#include "snap_tree.h"
#include "haversine.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <queue>
#include <stdexcept>
#include <vector>
#include <iostream>
#include <chrono>

// Internal helpers

// Entry dùng khi build: đại diện cho 1 cạnh
struct EdgeEntry {
    double   min_lat, min_lon, max_lat, max_lon; // BBox của đoạn u→v
    double   cx, cy;   // centroid để STR sort
    uint32_t u, v;
};

static double clamp(double val, double lo, double hi) {
    return val < lo ? lo : (val > hi ? hi : val);
}

// Squared Euclidean distance trên lat/lon (dùng để prune, không ra kết quả)
static double minDistSq(double qlat, double qlon, const STRNode& n) {
    double dlat = clamp(qlat, n.min_lat, n.max_lat) - qlat;
    double dlon = clamp(qlon, n.min_lon, n.max_lon) - qlon;
    return dlat * dlat + dlon * dlon;
}

// Chiếu điểm Q lên đoạn (U, V), trả về t ∈ [0,1] và tọa độ điểm chiếu
static void projectPointOnSegment(
    double qlat, double qlon,
    double ulat, double ulon,
    double vlat, double vlon,
    double& t_out, double& proj_lat, double& proj_lon)
{
    double dx = vlat - ulat;
    double dy = vlon - ulon;
    double len2 = dx * dx + dy * dy;

    if (len2 < 1e-18) {
        // Cạnh suy biến (u == v)
        t_out = 0.0;
        proj_lat = ulat;
        proj_lon = ulon;
        return;
    }

    double t = ((qlat - ulat) * dx + (qlon - ulon) * dy) / len2;
    t = clamp(t, 0.0, 1.0);

    proj_lat = ulat + t * dx;
    proj_lon = ulon + t * dy;
    t_out    = t;
}


// tạo cây STR-TREE với đệ quy từ dưới lên


// Xây một nhóm entries thành 1 leaf STRNode
static STRNode makeLeaf(const EdgeEntry& e) {
    STRNode n;
    n.min_lat     = e.min_lat;
    n.min_lon     = e.min_lon;
    n.max_lat     = e.max_lat;
    n.max_lon     = e.max_lon;
    n.child_start = 0;
    n.child_end   = LEAF_FLAG;   // is_leaf=true, childEnd=0
    n.edge_u      = e.u;
    n.edge_v      = e.v;
    return n;
}

// Xây internal node bao phủ [begin, end) trong nodes[]
static STRNode makeInternal(const std::vector<STRNode>& nodes,
                            uint32_t begin, uint32_t end) {
    STRNode n;
    n.min_lat = std::numeric_limits<double>::max();
    n.min_lon = std::numeric_limits<double>::max();
    n.max_lat = std::numeric_limits<double>::lowest();
    n.max_lon = std::numeric_limits<double>::lowest();

    for (uint32_t i = begin; i < end; ++i) {
        n.min_lat = std::min(n.min_lat, nodes[i].min_lat);
        n.min_lon = std::min(n.min_lon, nodes[i].min_lon);
        n.max_lat = std::max(n.max_lat, nodes[i].max_lat);
        n.max_lon = std::max(n.max_lon, nodes[i].max_lon);
    }
    n.child_start = begin;
    n.child_end   = end;   // bit31 = 0 → internal
    n.edge_u      = 0;
    n.edge_v      = 0;
    return n;
}

// Hàm sort 1 mảng các STRNode theo thuật toán Sort-Tile-Recursive (STR)
static void strSortNodes(std::vector<STRNode>& nodes, int lo, int hi, int leaf_cap) {
    int count = hi - lo;
    if (count <= leaf_cap) return;

    int num_chunks = (count + leaf_cap - 1) / leaf_cap;
    int grid_dim = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(num_chunks))));

    // Tính số chunk mỗi dải (strip) để strip_size luôn là bội số của leaf_cap
    // Đảm bảo rằng việc gom leaf_cap phần tử không bị cắt ngang dải (strip)
    int chunks_per_strip = (num_chunks + grid_dim - 1) / grid_dim;
    int strip_size = chunks_per_strip * leaf_cap;

    // Sort theo trục X (vĩ độ)
    std::sort(nodes.begin() + lo, nodes.begin() + hi,
              [](const STRNode& a, const STRNode& b) {
                  return (a.min_lat + a.max_lat) < (b.min_lat + b.max_lat);
              });

    for (int s = lo; s < hi; s += strip_size) {
        int s_end = std::min(s + strip_size, hi);
        // Sort theo trục Y (kinh độ)
        std::sort(nodes.begin() + s, nodes.begin() + s_end,
                  [](const STRNode& a, const STRNode& b) {
                      return (a.min_lon + a.max_lon) < (b.min_lon + b.max_lon);
                  });
    }
}

static void buildBottomUpSTR(const std::vector<EdgeEntry>& entries,
                             int leaf_cap,
                             std::vector<STRNode>& out_nodes)
{
    if (entries.empty()) return;

    std::vector<STRNode> current_level;
    current_level.reserve(entries.size());
    for (const auto& e : entries) {
        current_level.push_back(makeLeaf(e));
    }

    out_nodes.reserve(entries.size() + entries.size() / leaf_cap * 2);

    while (current_level.size() > 1) {
        strSortNodes(current_level, 0, static_cast<int>(current_level.size()), leaf_cap);

        uint32_t level_start = static_cast<uint32_t>(out_nodes.size());
        out_nodes.insert(out_nodes.end(), current_level.begin(), current_level.end());
        uint32_t level_end = static_cast<uint32_t>(out_nodes.size());

        int num_parents = (static_cast<int>(current_level.size()) + leaf_cap - 1) / leaf_cap;
        std::vector<STRNode> next_level;
        next_level.reserve(num_parents);

        for (uint32_t i = level_start; i < level_end; i += leaf_cap) {
            uint32_t end_i = std::min(i + static_cast<uint32_t>(leaf_cap), level_end);
            next_level.push_back(makeInternal(out_nodes, i, end_i));
        }
        current_level = std::move(next_level);
    }

    if (!current_level.empty()) {
        out_nodes.push_back(current_level[0]);
    }

    // Lật ngược mảng để root nằm ở index 0
    std::reverse(out_nodes.begin(), out_nodes.end());

    // Cập nhật lại child_start/child_end vì index đã bị đảo
    uint32_t sz = static_cast<uint32_t>(out_nodes.size());
    for (auto& n : out_nodes) {
        if (!isLeaf(n)) {
            uint32_t old_start = n.child_start;
            uint32_t old_end   = n.child_end & ~LEAF_FLAG; // An toàn cắt LEAF_FLAG
            n.child_start = sz - old_end;
            n.child_end   = sz - old_start;
        }
    }
}

SnapTree buildSnapTreeQuery(const CHGraphQuery& graph, int leaf_capacity) {
    // ... [giữ nguyên code cũ vì vẫn dùng trong một số trường hợp] ...
    // Note: To save space in prompt, I will keep the function above intact
    // and just append buildSnapTreeFromFile right after it.
    auto t0 = std::chrono::steady_clock::now();
    std::cout << "=== Building SnapTree from Online Graph ===\n";

    if (graph.num_nodes == 0) return {};

    std::vector<EdgeEntry> entries;
    entries.reserve(graph.fwd_hot.size());

    for (uint32_t u = 0; u < graph.num_nodes; ++u) {
        double ulat = graph.lat[u];
        double ulon = graph.lon[u];

        uint32_t fwd_start = graph.fwd_offset[u];
        uint32_t fwd_end   = graph.fwd_offset[u + 1];

        for (uint32_t i = fwd_start; i < fwd_end; i++) {
            uint32_t v = graph.fwd_hot[i].target;
            
            // Skip shortcuts!
            if (graph.fwd_cold[i].mid_node != (uint32_t)-1) continue;

            if (v < graph.num_nodes) {
                double vlat = graph.lat[v];
                double vlon = graph.lon[v];

                EdgeEntry ee;
                ee.min_lat = std::min(ulat, vlat);
                ee.min_lon = std::min(ulon, vlon);
                ee.max_lat = std::max(ulat, vlat);
                ee.max_lon = std::max(ulon, vlon);
                ee.cx = (ulat + vlat) * 0.5;
                ee.cy = (ulon + vlon) * 0.5;
                ee.u  = u;
                ee.v  = v;
                entries.push_back(ee);
            }
        }
    }

    std::cout << "  Filtering left " << entries.size() << " actual road segments (non-shortcuts)\n";

    SnapTree tree;
    if (entries.empty()) return tree;

    buildBottomUpSTR(entries, leaf_capacity, tree.nodes);
    
    auto t1 = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(t1 - t0).count();
    std::cout << "=== SnapTree built in " << total_sec << " seconds, " 
              << tree.nodes.size() << " nodes created ===\n\n";

    return tree;
}

#pragma pack(push, 1)
struct FileHeaderV2 {
    uint32_t magic;
    uint32_t version;
    uint32_t num_nodes;
    uint32_t num_fwd_edges;
    uint32_t num_bwd_edges;
};
struct SerNodeV2 {
    double lat;
    double lon;
    int32_t rank;
    uint32_t fwd_offset;  
    uint32_t bwd_offset;  
};
#pragma pack(pop)

SnapTree buildSnapTreeFromFile(const std::string& filepath, int leaf_capacity) {
    auto t0 = std::chrono::steady_clock::now();
    std::cout << "=== Building SnapTree directly from file (Low RAM mode) ===\n";

    FILE* f = fopen(filepath.c_str(), "rb");
    if (!f) {
        std::cerr << "  ERROR: Cannot open " << filepath << "\n";
        return {};
    }

    FileHeaderV2 header;
    if (fread(&header, sizeof(header), 1, f) != 1) {
        fclose(f); return {};
    }
    
    uint32_t n = header.num_nodes;
    std::cout << "  Reading " << n << " nodes...\n";

    std::vector<double> lats(n);
    std::vector<double> lons(n);
    std::vector<uint32_t> fwd_offsets(n + 1);
    std::vector<uint32_t> bwd_offsets(n + 1);

    std::vector<SerNodeV2> ser_nodes(n + 1);
    fread(ser_nodes.data(), sizeof(SerNodeV2), n + 1, f);
    
    for (uint32_t i = 0; i < n; i++) {
        lats[i] = ser_nodes[i].lat;
        lons[i] = ser_nodes[i].lon;
    }
    for (uint32_t i = 0; i <= n; i++) {
        fwd_offsets[i] = ser_nodes[i].fwd_offset;
        bwd_offsets[i] = ser_nodes[i].bwd_offset;
    }
    
    uint64_t offset_fwd_hot = sizeof(FileHeaderV2) + (n + 1) * sizeof(SerNodeV2);
    uint64_t offset_fwd_cold = offset_fwd_hot + header.num_fwd_edges * sizeof(HotEdge);

    FILE* f_hot = fopen(filepath.c_str(), "rb");
    FILE* f_cold = fopen(filepath.c_str(), "rb");
    
    _fseeki64(f_hot, offset_fwd_hot, SEEK_SET);
    _fseeki64(f_cold, offset_fwd_cold, SEEK_SET);

    std::cout << "  Streaming " << header.num_fwd_edges << " forward edges...\n";
    std::vector<EdgeEntry> entries;
    // We know from a previous run that there are exactly 88083243 non-shortcut edges.
    // Reserve enough memory (100M) to prevent vector reallocation (which causes OOM).
    entries.reserve(100000000);

    const int BATCH = 1000000;
    std::vector<HotEdge> hot_batch(BATCH);
    std::vector<ColdEdge> cold_batch(BATCH);

    uint32_t edges_read = 0;
    uint32_t current_u = 0;

    while (edges_read < header.num_fwd_edges) {
        uint32_t to_read = std::min((uint32_t)BATCH, header.num_fwd_edges - edges_read);
        fread(hot_batch.data(), sizeof(HotEdge), to_read, f_hot);
        fread(cold_batch.data(), sizeof(ColdEdge), to_read, f_cold);

        for (uint32_t i = 0; i < to_read; i++) {
            uint32_t global_idx = edges_read + i;
            // Advance current_u if needed
            while (current_u < n && global_idx >= fwd_offsets[current_u + 1]) {
                current_u++;
            }

            if (cold_batch[i].mid_node == (uint32_t)-1) {
                uint32_t v = hot_batch[i].target;
                if (v < n) {
                    double ulat = lats[current_u];
                    double ulon = lons[current_u];
                    double vlat = lats[v];
                    double vlon = lons[v];

                    EdgeEntry ee;
                    ee.min_lat = std::min(ulat, vlat);
                    ee.min_lon = std::min(ulon, vlon);
                    ee.max_lat = std::max(ulat, vlat);
                    ee.max_lon = std::max(ulon, vlon);
                    ee.cx = (ulat + vlat) * 0.5;
                    ee.cy = (ulon + vlon) * 0.5;
                    ee.u  = current_u;
                    ee.v  = v;
                    entries.push_back(ee);
                }
            }
        }
        edges_read += to_read;
        printf("Read %u edges, entries=%u\r", edges_read, (uint32_t)entries.size());
        fflush(stdout);
    }
    printf("\n");

    fclose(f_hot);
    fclose(f_cold);
    fclose(f);

    std::cout << "  Filtering left " << entries.size() << " actual road segments (non-shortcuts)\n";

    SnapTree tree;
    if (entries.empty()) return tree;

    // Free memory before recursive build
    lats.clear(); lats.shrink_to_fit();
    lons.clear(); lons.shrink_to_fit();
    fwd_offsets.clear(); fwd_offsets.shrink_to_fit();

    buildBottomUpSTR(entries, leaf_capacity, tree.nodes);
    
    auto t1 = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(t1 - t0).count();
    std::cout << "=== SnapTree built in " << total_sec << " seconds, " 
              << tree.nodes.size() << " nodes created ===\n\n";

    return tree;
}

// ---------------------------------------------------------
// buildSnapTree
// ---------------------------------------------------------
SnapTree buildSnapTree(const CHGraph& graph, int leaf_capacity) {
    if (graph.num_nodes == 0) return {};

    // Thu thập tất cả forward edges
    std::vector<EdgeEntry> entries;
    entries.reserve(graph.fwd_edges.size());

    for (uint32_t u = 0; u < graph.num_nodes; ++u) {
        double ulat = graph.lat[u];
        double ulon = graph.lon[u];

        // Duyệt linked list forward edges của u
        uint32_t eidx = graph.first_fwd[u];
        while (eidx != NO_EDGE) {
            const CHEdge& e = graph.fwd_edges[eidx];
            uint32_t v = e.target;

            if (v < graph.num_nodes) {
                double vlat = graph.lat[v];
                double vlon = graph.lon[v];

                EdgeEntry ee;
                ee.min_lat = std::min(ulat, vlat);
                ee.min_lon = std::min(ulon, vlon);
                ee.max_lat = std::max(ulat, vlat);
                ee.max_lon = std::max(ulon, vlon);
                ee.cx = (ulat + vlat) * 0.5;
                ee.cy = (ulon + vlon) * 0.5;
                ee.u  = u;
                ee.v  = v;
                entries.push_back(ee);
            }

            eidx = e.next;
        }
    }

    if (entries.empty()) return {};

    uint32_t total_edges = static_cast<uint32_t>(entries.size());

    // Ước tính số STRNode: ~1.3 × (N / leaf_capacity) cho cây cân bằng
    std::vector<STRNode> flat_nodes;
    
    buildBottomUpSTR(entries, leaf_capacity, flat_nodes);

    SnapTree tree;
    tree.nodes           = std::move(flat_nodes);
    tree.num_graph_nodes = graph.num_nodes;
    tree.num_graph_edges = total_edges;
    return tree;
}


// snapNearest — Best-First Search với pruning
//thuật toán Best First Search với Priority queue và cắt tỉa nếu >d_best

template <typename GraphT>
static SnapResult snapNearestImpl(const SnapTree& tree,
                                  const GraphT&  graph,
                                  double qlat, double qlon)
{
    SnapResult best;
    best.dist_m = std::numeric_limits<double>::max();

    if (tree.nodes.empty()) return best;

    // Priority queue: {min_dist_sq, node_idx}
    using PQItem = std::pair<double, uint32_t>;
    std::priority_queue<PQItem, std::vector<PQItem>, std::greater<PQItem>> pq;

    // Push root (index 0)
    double root_dist = minDistSq(qlat, qlon, tree.nodes[0]);
    pq.push({root_dist, 0});

    while (!pq.empty()) {
        auto [dist_sq, idx] = pq.top();
        pq.pop();

        // Pruning: nếu dist đến bbox >= best, bỏ toàn bộ subtree
        // best.dist_m tính bằng mét, dist_sq tính bằng degree²
        // Xấp xỉ: 1 degree lat ≈ 111km → 1 degree² ≈ (111000)² m²
        double best_sq_approx = (best.dist_m / 111000.0) * (best.dist_m / 111000.0);
        if (dist_sq >= best_sq_approx && best.valid) continue;

        const STRNode& node = tree.nodes[idx];

        if (isLeaf(node)) {
            // Tính khoảng cách thực đến cạnh (edge_u → edge_v)
            uint32_t u = node.edge_u;
            uint32_t v = node.edge_v;

            if (u >= graph.num_nodes || v >= graph.num_nodes) continue;

            double ulat = graph.lat[u], ulon = graph.lon[u];
            double vlat = graph.lat[v], vlon = graph.lon[v];

            double t, proj_lat, proj_lon;
            projectPointOnSegment(qlat, qlon, ulat, ulon, vlat, vlon,
                                  t, proj_lat, proj_lon);

            double dist = haversine(qlat, qlon, proj_lat, proj_lon);

            if (dist < best.dist_m) {
                best.dist_m  = dist;
                best.edge_u  = u;
                best.edge_v  = v;
                best.t       = t;
                best.proj_lat = proj_lat;
                best.proj_lon = proj_lon;
                best.valid   = true;
            }
        } else {
            // Internal node: push tất cả children
            for (uint32_t c = childStart(node); c < childEnd(node); ++c) {
                double cd = minDistSq(qlat, qlon, tree.nodes[c]);
                pq.push({cd, c});
            }
        }
    }

    return best;
}

SnapResult snapNearest(const SnapTree& tree, const CHGraph& graph, double qlat, double qlon) {
    return snapNearestImpl(tree, graph, qlat, qlon);
}

SnapResult snapNearestQuery(const SnapTree& tree, const CHGraphQuery& graph, double qlat, double qlon) {
    return snapNearestImpl(tree, graph, qlat, qlon);
}

// Lưu / Load cây dưới dạng (.bin) , với cấu trúc CSR để khởi động nhanh hơn

static const char SNAP_MAGIC[4] = {'S', 'N', 'A', 'P'};
static const uint32_t SNAP_VERSION = 1;

bool saveSnapTree(const SnapTree& tree, const std::string& filepath) {
    FILE* f = fopen(filepath.c_str(), "wb");
    if (!f) return false;

    // Header
    fwrite(SNAP_MAGIC, 1, 4, f);
    uint32_t ver      = SNAP_VERSION;
    uint32_t ng_nodes = tree.num_graph_nodes;
    uint32_t ng_edges = tree.num_graph_edges;
    uint64_t n_str    = static_cast<uint64_t>(tree.nodes.size());

    fwrite(&ver,      4, 1, f);
    fwrite(&ng_nodes, 4, 1, f);
    fwrite(&ng_edges, 4, 1, f);
    fwrite(&n_str,    8, 1, f);

    // Body: flat array
    if (!tree.nodes.empty()) {
        fwrite(tree.nodes.data(), sizeof(STRNode), tree.nodes.size(), f);
    }

    fclose(f);
    return true;
}

bool loadSnapTree(SnapTree& tree, const std::string& filepath) {
    FILE* f = fopen(filepath.c_str(), "rb");
    if (!f) return false;

    char magic[4];
    fread(magic, 1, 4, f);
    if (memcmp(magic, SNAP_MAGIC, 4) != 0) { fclose(f); return false; }

    uint32_t ver;
    fread(&ver, 4, 1, f);
    if (ver != SNAP_VERSION) { fclose(f); return false; }

    uint32_t ng_nodes, ng_edges;
    uint64_t n_str;
    fread(&ng_nodes, 4, 1, f);
    fread(&ng_edges, 4, 1, f);
    fread(&n_str,    8, 1, f);

    tree.num_graph_nodes = ng_nodes;
    tree.num_graph_edges = ng_edges;
    tree.nodes.resize(static_cast<size_t>(n_str));

    if (n_str > 0) {
        fread(tree.nodes.data(), sizeof(STRNode), static_cast<size_t>(n_str), f);
    }

    fclose(f);
    return true;
}
