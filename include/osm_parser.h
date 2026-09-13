#pragma once
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

// Forward declaration
class GeocodingDB;

// ============================================================
// Data types output from OSM parsing
// ============================================================

struct LatLon {
    double lat = 0.0;
    double lon = 0.0;
};

struct WayData {
    std::vector<int64_t> node_ids;  // Ordered list of OSM node IDs
    std::string highway_type;       // e.g. "primary", "motorway", "footway"
    bool oneway = false;
};

struct ParsedData {
    // node_ids and node_coords are parallel arrays sorted by OSM ID.
    // The index in these arrays is the "compact node ID" (0..N-1) used in the graph.
    std::vector<int64_t> node_ids;      // Sorted OSM node IDs
    std::vector<LatLon> node_coords;    // Corresponding coordinates
    std::vector<WayData> ways;          // Highway ways
};

// Accepted highway types organized by transport module:
// Module 1: Mixed (car + motorbike)
// Module 2: Car only (motorway/expressway)
// Module 3: Walking + Cycling
static const std::unordered_set<std::string> ACCEPTED_HIGHWAYS = {
    // Đường dành riêng cho ô tô (đường cao tốc)
    "motorway", "motorway_link",
    // Mixed (car + motorbike)
    "trunk", "trunk_link",
    "primary", "primary_link",
    "secondary", "secondary_link",
    "tertiary", "tertiary_link",
    "residential", "unclassified",
    "service", "living_street",
    // Walking + Cycling
    "footway", "pedestrian", "cycleway", "path", "steps", "track"
};

// Bounding boxes for sovereignty data labeling
inline bool isInHoangSa(double lat, double lon) {
    return (lat >= 15.0 && lat <= 17.5 && lon >= 111.0 && lon <= 113.0);
}
inline bool isInTruongSa(double lat, double lon) {
    return (lat >= 7.0 && lat <= 12.0 && lon >= 111.0 && lon <= 118.0);
}

// Parse OSM PBF file using 2-pass approach.
// POIs are streamed directly into GeocodingDB if provided.
ParsedData parseOSM(const std::string& input_file, GeocodingDB* geo_db = nullptr);
