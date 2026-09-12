#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// Transport Mode flags (bitmask)
enum TransportMode : uint8_t {
    MODE_CAR        = 0x01,
    MODE_MOTORBIKE  = 0x02,
    MODE_BICYCLE    = 0x04,
    MODE_PEDESTRIAN = 0x08,
};

inline uint8_t getTransportMode(const std::string& highway_type) {
    if (highway_type == "motorway" || highway_type == "motorway_link") return MODE_CAR;
    if (highway_type == "footway" || highway_type == "pedestrian" || highway_type == "steps") return MODE_PEDESTRIAN;
    if (highway_type == "cycleway" || highway_type == "path") return MODE_BICYCLE | MODE_PEDESTRIAN;
    if (highway_type == "track") return MODE_CAR | MODE_MOTORBIKE | MODE_BICYCLE | MODE_PEDESTRIAN;
    return MODE_CAR | MODE_MOTORBIKE;
}

inline float getSpeedKmh(const std::string& highway_type) {
    if (highway_type == "motorway")       return 100.0f;
    if (highway_type == "motorway_link")  return 60.0f;
    if (highway_type == "trunk")          return 80.0f;
    if (highway_type == "trunk_link")     return 50.0f;
    if (highway_type == "primary")        return 60.0f;
    if (highway_type == "primary_link")   return 40.0f;
    if (highway_type == "secondary")      return 50.0f;
    if (highway_type == "secondary_link") return 35.0f;
    if (highway_type == "tertiary")       return 40.0f;
    if (highway_type == "tertiary_link")  return 30.0f;
    if (highway_type == "residential")    return 30.0f;
    if (highway_type == "unclassified")   return 30.0f;
    if (highway_type == "service")        return 20.0f;
    if (highway_type == "living_street")  return 20.0f;
    if (highway_type == "footway")        return 5.0f;
    if (highway_type == "pedestrian")     return 5.0f;
    if (highway_type == "cycleway")       return 15.0f;
    if (highway_type == "path")           return 5.0f;
    if (highway_type == "steps")          return 3.0f;
    if (highway_type == "track")          return 20.0f;
    return 30.0f;
}


// Constants
static constexpr uint32_t NO_SHORTCUT = std::numeric_limits<uint32_t>::max();
static constexpr uint32_t NO_EDGE = std::numeric_limits<uint32_t>::max();


// CH Edge for PREPROCESSING (Edge Pool linked list)

struct CHEdge {
    uint32_t target;
    float weight;
    uint32_t mid_node;
    uint8_t transport_mode;
    uint32_t next;  // linked list pointer

    bool isShortcut() const {
        return mid_node != NO_SHORTCUT;
    }
};


// CH Graph for PREPROCESSING (Edge Pool)
// Optimized for dynamic shortcut insertion during contraction.

struct CHGraph {
    uint32_t num_nodes = 0;

    std::vector<double> lat;
    std::vector<double> lon;
    std::vector<int32_t> rank;
    std::vector<bool> contracted;

    // Edge Pool (linked list)
    std::vector<CHEdge> fwd_edges;
    std::vector<uint32_t> first_fwd;
    std::vector<CHEdge> bwd_edges;
    std::vector<uint32_t> first_bwd;

    std::vector<int64_t> osm_id;

    void resize(uint32_t n) {
        num_nodes = n;
        lat.resize(n);
        lon.resize(n);
        rank.assign(n, -1);
        contracted.assign(n, false);
        first_fwd.assign(n, NO_EDGE);
        first_bwd.assign(n, NO_EDGE);
        fwd_edges.reserve(n * 4);
        bwd_edges.reserve(n * 4);
        osm_id.resize(n);
    }

    void addFwdEdge(uint32_t u, uint32_t v, float weight, uint32_t mid, uint8_t mode) {
        uint32_t idx = static_cast<uint32_t>(fwd_edges.size());
        fwd_edges.push_back({v, weight, mid, mode, first_fwd[u]});
        first_fwd[u] = idx;
    }

    void addBwdEdge(uint32_t u, uint32_t v, float weight, uint32_t mid, uint8_t mode) {
        uint32_t idx = static_cast<uint32_t>(bwd_edges.size());
        bwd_edges.push_back({v, weight, mid, mode, first_bwd[u]});
        first_bwd[u] = idx;
    }

    size_t countEdges() const { return fwd_edges.size(); }

    size_t countShortcuts() const {
        size_t total = 0;
        for (const auto& e : fwd_edges) {
            if (e.isShortcut()) total++;
        }
        return total;
    }
};


// SoA Edge structs for ONLINE QUERY (CSR layout)
// Optimized for Dijkstra cache performance.

// HOT: touched every Dijkstra iteration
#pragma pack(push, 1)
struct HotEdge {
    uint32_t target;          // 4B
    float weight;             // 4B
    uint8_t transport_mode;   // 1B
    uint8_t padding[3];       // 3B  → total 12 bytes aligned
};

// COLD: touched only during path unpacking
struct ColdEdge {
    uint32_t mid_node;        // 4B  (NO_SHORTCUT if original edge)
};
#pragma pack(pop)

// CSR + SoA query graph (online server — loaded from hanoi_ch.bin v2)
// File format: SerNodeHot(N+1)×12B → SerNodeCold(N)×16B → FwdHot/Cold → BwdHot/Cold
// In-memory layout: separate vectors → Dijkstra only touches rank[] + fwd/bwd_offset[]
struct CHGraphQuery {
    uint32_t num_nodes = 0;

    // Node data
    std::vector<double> lat;
    std::vector<double> lon;
    std::vector<int32_t> rank;

    // CSR offsets (size = num_nodes + 1)
    std::vector<uint32_t> fwd_offset;
    std::vector<uint32_t> bwd_offset;

    // Forward SoA
    std::vector<HotEdge> fwd_hot;
    std::vector<ColdEdge> fwd_cold;

    // Backward SoA
    std::vector<HotEdge> bwd_hot;
    std::vector<ColdEdge> bwd_cold;
};

struct ParsedData;
void buildGraph(CHGraph& graph, const ParsedData& data);
