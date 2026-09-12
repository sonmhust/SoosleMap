#pragma once
#include "graph.h"
#include <cstdint>
#include <string>
#include <vector>

// STR-Tree (Sort-Tile-Recursive R-Tree) — Snap-to-Edge
// Mỗi leaf bọc một cạnh đồ thị (đoạn thẳng u→v).
// snapNearest() trả về điểm chiếu P trên cạnh gần nhất.

// Một node trong flat array STR-Tree (48 bytes, cache-line aligned)
// Bit31 của child_end dùng làm cờ is_leaf để tránh padding.
struct STRNode {
    double   min_lat;      //  8B
    double   min_lon;      //  8B
    double   max_lat;      //  8B
    double   max_lon;      //  8B  → BBox tổng = 32B
    uint32_t child_start;  //  4B  (internal: first child idx; leaf: unused)
    uint32_t child_end;    //  4B  (internal: last+1 | bit31=is_leaf flag)
    uint32_t edge_u;       //  4B  (leaf only: compact node index u)
    uint32_t edge_v;       //  4B  (leaf only: compact node index v)
                           // tổng = 48B
};

static constexpr uint32_t LEAF_FLAG = 0x80000000u;

inline bool     isLeaf     (const STRNode& n) { return (n.child_end & LEAF_FLAG) != 0; }
inline uint32_t childStart (const STRNode& n) { return n.child_start; }
inline uint32_t childEnd   (const STRNode& n) { return n.child_end & ~LEAF_FLAG; }

struct SnapTree {
    std::vector<STRNode> nodes;          // flat array, nodes[0] = root
    uint32_t             num_graph_nodes = 0;
    uint32_t             num_graph_edges = 0;
};

// Kết quả snap: điểm chiếu P trên cạnh (edge_u → edge_v)
struct SnapResult {
    uint32_t edge_u;       // compact node index u
    uint32_t edge_v;       // compact node index v
    double   t;            // tham số nội suy ∈ [0,1]: P = u + t*(v-u)
    double   proj_lat;     // tọa độ điểm chiếu P (đã nội suy)
    double   proj_lon;
    double   dist_m;       // khoảng cách Haversine(query, P) tính bằng mét
    bool     valid = false;
};

// ============================================================
// Public API
// ============================================================

// Build tree from offline graph
SnapTree buildSnapTree(const CHGraph& graph, int leaf_capacity = 16);

// Build tree from online query graph (filtering shortcuts)
SnapTree buildSnapTreeQuery(const CHGraphQuery& graph, int leaf_capacity = 16);

// Build tree from binary file directly to save RAM
SnapTree buildSnapTreeFromFile(const std::string& filepath, int leaf_capacity = 16);

// Tìm điểm trên cạnh gần nhất với (query_lat, query_lon).
// Trả SnapResult.valid = false nếu tree rỗng.
SnapResult snapNearest(const SnapTree& tree,
                       const CHGraph&  graph,
                       double query_lat, double query_lon);

SnapResult snapNearestQuery(const SnapTree& tree,
                            const CHGraphQuery& graph,
                            double query_lat, double query_lon);

// Serialize/Deserialize binary blob (fwrite/fread 1 lần, zero-parse).
// Format: magic[4]="SNAP" | version:u32 | num_graph_nodes:u32 |
//         num_graph_edges:u32 | num_str_nodes:u64 | STRNode[]
bool saveSnapTree(const SnapTree& tree, const std::string& filepath);
bool loadSnapTree(SnapTree& tree,       const std::string& filepath);
