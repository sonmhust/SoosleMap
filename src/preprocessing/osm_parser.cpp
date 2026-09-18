#define NOMINMAX
#include "osm_parser.h"
#include "geocoding_db.h"
#include "address_parser.h"
#include <iostream>
#include <chrono>
#include <algorithm>

#include <osmium/handler.hpp>
#include <osmium/io/pbf_input.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/visitor.hpp>

// Pending addr:interpolation range (cần resolve tọa độ sau Pass 2)
struct AddrInterp {
    int64_t     node_start = 0;   // OSM node ID đầu way
    int64_t     node_end   = 0;   // OSM node ID cuối way
    std::string street;           // addr:street gốc
    int         num_start  = -1;  // addr:housenumber của node_start
    int         num_end    = -1;  // addr:housenumber của node_end
    std::string interp_type;      // "all", "even", "odd"
};

// ============================================================
// Pass 1: Scan Ways — collect highway ways and their node IDs
// ============================================================
class HighwayWayHandler : public osmium::handler::Handler {
public:
    std::vector<int64_t>   valid_nodes;
    std::vector<WayData>    ways;
    std::vector<AddrInterp> pending_interps;  // addr:interpolation ways
    GeocodingDB* geo_db;

    HighwayWayHandler(GeocodingDB* db) : geo_db(db) {
        // Reserve memory to prevent massive reallocations (Culprit 3)
        valid_nodes.reserve(45000000);
        ways.reserve(3000000);
        pending_interps.reserve(500000);
    }

    void way(const osmium::Way& way) {
        // -----------------------------------------------
        // Handle addr:interpolation ways
        // -----------------------------------------------
        const char* interp = way.tags().get_value_by_key("addr:interpolation");
        if (interp && way.nodes().size() >= 2) {
            const char* street = way.tags().get_value_by_key("addr:street");
            if (street) {
                // Tọa độ sẽ được resolve sau Pass 2
                // Số nhà nằm trên 2 node đầu/cuối của way này
                AddrInterp ai;
                ai.node_start  = way.nodes().front().ref();
                ai.node_end    = way.nodes().back().ref();
                ai.street      = street;
                ai.interp_type = interp;
                // num_start/num_end sẽ được lấy từ node tags trong Pass 2
                // Lưu node IDs vào valid_nodes để Pass 2 đọc tọa độ
                valid_nodes.push_back(ai.node_start);
                valid_nodes.push_back(ai.node_end);
                pending_interps.push_back(ai);
            }
            return;  // không phải highway
        }

        // -----------------------------------------------
        // Handle highway ways (existing logic)
        // -----------------------------------------------
        const char* highway = way.tags().get_value_by_key("highway");
        if (!highway) return;

        std::string hw_type(highway);
        if (ACCEPTED_HIGHWAYS.find(hw_type) == ACCEPTED_HIGHWAYS.end()) return;

        WayData wd;
        wd.highway_type = hw_type;

        // Check oneway
        // Check oneway
        bool is_reverse = false;
        const char* oneway = way.tags().get_value_by_key("oneway");
        if (oneway) {
            std::string ow(oneway);
            if (ow == "-1" || ow == "reverse") {
                wd.oneway = true;
                is_reverse = true;
            } else {
                wd.oneway = (ow == "yes" || ow == "1" || ow == "true");
            }
        }
        
        const char* junction = way.tags().get_value_by_key("junction");
        if (junction && std::string(junction) == "roundabout") {
            wd.oneway = true; // Roundabouts are implicitly oneway
        }
        if (hw_type == "motorway" || hw_type == "motorway_link") {
            wd.oneway = true;
        }

        // Collect node IDs
        for (const auto& node_ref : way.nodes()) {
            int64_t nid = node_ref.ref();
            wd.node_ids.push_back(nid);
            valid_nodes.push_back(nid);
        }

        // Reverse if oneway=-1
        if (is_reverse) {
            std::reverse(wd.node_ids.begin(), wd.node_ids.end());
        }

        ways.push_back(std::move(wd));

        // Stream street POI immediately (lat/lon = 0 since it's a line)
        if (geo_db) {
            const char* name    = way.tags().get_value_by_key("name");
            const char* name_vi = way.tags().get_value_by_key("name:vi");
            if (name || name_vi) {
                POIData poi;
                poi.osm_id    = way.id();
                if (name)    poi.name    = name;
                if (name_vi) poi.name_vi = name_vi;
                poi.name_norm = normalizeVietnamese(
                    poi.name_vi.empty() ? poi.name : poi.name_vi);
                poi.type = std::string("street:") + hw_type;
                poi.lat  = 0.0;
                poi.lon  = 0.0;
                geo_db->insertPOI(poi);
            }
        }
    }
};

// ============================================================
// Pass 2: Scan Nodes — get coordinates for valid nodes + POIs
// ============================================================
class NodeCoordHandler : public osmium::handler::Handler {
    const std::vector<int64_t>& valid_nodes; // Must be sorted
    GeocodingDB* geo_db;

public:
    std::vector<LatLon>& node_coords;
    int coord_count = 0;

    NodeCoordHandler(const std::vector<int64_t>& vn, std::vector<LatLon>& nc, GeocodingDB* db)
        : valid_nodes(vn), geo_db(db), node_coords(nc) {}

    void node(const osmium::Node& node) {
        double lat = node.location().lat();
        double lon = node.location().lon();

        // Sovereignty data labeling
        const char* name = node.tags().get_value_by_key("name");
        std::string final_name = name ? name : "";
        std::string final_name_vi = "";
        const char* name_vi_tag = node.tags().get_value_by_key("name:vi");
        if (name_vi_tag) final_name_vi = name_vi_tag;

        if (isInHoangSa(lat, lon)) {
            final_name = "Quần đảo Hoàng Sa";
            final_name_vi = "Quần đảo Hoàng Sa";
        } else if (isInTruongSa(lat, lon)) {
            final_name = "Quần đảo Trường Sa";
            final_name_vi = "Quần đảo Trường Sa";
        }

        // Match against valid_nodes
        auto it = std::lower_bound(valid_nodes.begin(), valid_nodes.end(), node.id());
        if (it != valid_nodes.end() && *it == node.id()) {
            size_t idx = std::distance(valid_nodes.begin(), it);
            node_coords[idx] = {lat, lon};
            coord_count++;
        }

        // Stream POI directly to SQLite (Culprit 2)
        if (geo_db) {
            const char* amenity = node.tags().get_value_by_key("amenity");
            const char* shop    = node.tags().get_value_by_key("shop");
            const char* tourism = node.tags().get_value_by_key("tourism");

            // Đọc addr:housenumber để sau này gán vào AddrInterp
            const char* housenumber = node.tags().get_value_by_key("addr:housenumber");
            (void)housenumber; // reserved for future use in Pass 2 node handler

            bool has_poi_tag = (amenity || shop || tourism);
            bool has_name    = !final_name.empty();

            if (has_name || has_poi_tag) {
                POIData poi;
                poi.osm_id    = node.id();
                poi.name      = final_name;
                poi.name_vi   = final_name_vi;
                poi.name_norm = normalizeVietnamese(
                    final_name_vi.empty() ? final_name : final_name_vi);
                poi.lat  = lat;
                poi.lon  = lon;

                if (amenity)      poi.type = amenity;
                else if (shop)    poi.type = std::string("shop:") + shop;
                else if (tourism) poi.type = std::string("tourism:") + tourism;
                else              poi.type = "place";

                geo_db->insertPOI(poi);
            }
        }
    }
};

// ============================================================
// Main parsing function
// ============================================================
ParsedData parseOSM(const std::string& input_file, GeocodingDB* geo_db) {
    ParsedData data;
    auto t0 = std::chrono::steady_clock::now();

    // === PASS 1: Scan Ways ===
    std::cout << "=== PASS 1: Scanning Ways ===\n";
    HighwayWayHandler way_handler(geo_db);
    {
        osmium::io::Reader reader(input_file, osmium::osm_entity_bits::way);
        osmium::apply(reader, way_handler);
        reader.close();
    }
    std::cout << "  Found " << way_handler.ways.size() << " highway ways\n";

    // Sort and deduplicate node IDs
    std::cout << "  Sorting and deduplicating node IDs...\n";
    std::sort(way_handler.valid_nodes.begin(), way_handler.valid_nodes.end());
    way_handler.valid_nodes.erase(
        std::unique(way_handler.valid_nodes.begin(), way_handler.valid_nodes.end()),
        way_handler.valid_nodes.end()
    );
    std::cout << "  Found " << way_handler.valid_nodes.size() << " unique node IDs\n";

    auto t1 = std::chrono::steady_clock::now();
    double pass1_sec = std::chrono::duration<double>(t1 - t0).count();
    std::cout << "  Pass 1 completed in " << pass1_sec << " seconds\n\n";

    // Transfer node_ids and initialize coords
    data.node_ids = std::move(way_handler.valid_nodes);
    data.node_coords.resize(data.node_ids.size(), {0.0, 0.0});

    // === PASS 2: Scan Nodes ===
    std::cout << "=== PASS 2: Scanning Nodes ===\n";
    NodeCoordHandler node_handler(data.node_ids, data.node_coords, geo_db);
    {
        osmium::io::Reader reader(input_file, osmium::osm_entity_bits::node);
        osmium::apply(reader, node_handler);
        reader.close();
    }
    std::cout << "  Parsed " << node_handler.coord_count << " highway node coordinates\n";

    auto t2 = std::chrono::steady_clock::now();
    double pass2_sec = std::chrono::duration<double>(t2 - t1).count();
    std::cout << "  Pass 2 completed in " << pass2_sec << " seconds\n\n";

    data.ways = std::move(way_handler.ways);

    // ===================================================
    // Post-Pass 2: Resolve addr:interpolation ranges
    // node_ids đã có tọa độ → lookup node_start/end → insert addr_ranges
    // ===================================================
    if (geo_db && !way_handler.pending_interps.empty()) {
        std::cout << "=== Resolving " << way_handler.pending_interps.size()
                  << " addr:interpolation ranges ===\n";
        int resolved = 0;
        for (const auto& ai : way_handler.pending_interps) {
            // Binary search node_start trong data.node_ids
            auto lookup = [&](int64_t nid, double& lat_out, double& lon_out) -> bool {
                auto it = std::lower_bound(data.node_ids.begin(), data.node_ids.end(), nid);
                if (it == data.node_ids.end() || *it != nid) return false;
                size_t idx = std::distance(data.node_ids.begin(), it);
                lat_out = data.node_coords[idx].lat;
                lon_out = data.node_coords[idx].lon;
                return true;
            };

            double lat_s, lon_s, lat_e, lon_e;
            if (!lookup(ai.node_start, lat_s, lon_s)) continue;
            if (!lookup(ai.node_end,   lat_e, lon_e)) continue;
            if (ai.num_start < 0 || ai.num_end < 0)   continue;
            if (ai.num_start >= ai.num_end)            continue;

            AddrRange range;
            range.street      = ai.street;
            range.street_norm = normalizeVietnamese(ai.street);
            range.num_start   = ai.num_start;
            range.num_end     = ai.num_end;
            range.lat_start   = lat_s;
            range.lon_start   = lon_s;
            range.lat_end     = lat_e;
            range.lon_end     = lon_e;
            range.interp_type = ai.interp_type;
            geo_db->insertAddrRange(range);
            resolved++;
        }
        std::cout << "  Resolved and inserted " << resolved << " addr ranges\n\n";
    }

    auto t3 = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(t3 - t0).count();
    std::cout << "=== OSM Parsing complete in " << total_sec << " seconds ===\n\n";

    return data;
}

