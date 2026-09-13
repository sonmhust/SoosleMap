#include "routing.h"
#include "haversine.h"
#include <queue>
#include <limits>
#include <algorithm>
#include <iostream>

static constexpr float INF_WEIGHT = std::numeric_limits<float>::max();

// Cấu trúc trạng thái cho Priority Queue
struct PQState {
    float dist;
    uint32_t node;
    bool operator>(const PQState& other) const {
        return dist > other.dist;
    }
};

// ===== Workspace tái sử dụng giữa các query (Amortized Allocation) =====
// Thay vì allocate ~30 MB (4 vector × 1.85M phần tử) mới cho mỗi query,
// workspace allocate 1 lần duy nhất khi query đầu tiên chạy.
// Sau mỗi query, chỉ reset các node đã chạm (~1000 node) — O(visited) thay vì O(N).

struct QueryWorkspace {
    std::vector<float> dist_f, dist_b;
    std::vector<uint32_t> parent_f, parent_b;
    std::vector<uint32_t> touched;  // danh sách node đã bị sửa đổi
    uint32_t capacity = 0;

    // Allocate 1 lần duy nhất (hoặc khi graph đổi kích thước)
    void init(uint32_t n) {
        capacity = n;
        dist_f.assign(n, INF_WEIGHT);
        dist_b.assign(n, INF_WEIGHT);
        parent_f.assign(n, NO_EDGE);
        parent_b.assign(n, NO_EDGE);
        touched.reserve(8192);
    }

    // Reset chỉ các node đã chạm về trạng thái ban đầu — O(visited)
    void reset() {
        for (uint32_t u : touched) {
            dist_f[u] = INF_WEIGHT;
            dist_b[u] = INF_WEIGHT;
            parent_f[u] = NO_EDGE;
            parent_b[u] = NO_EDGE;
        }
        touched.clear();
    }

    // Đánh dấu node đã chạm (gọi TRƯỚC khi ghi dist/parent)
    // Chỉ thêm vào danh sách nếu node chưa từng bị chạm trong query này
    void touch(uint32_t u) {
        if (dist_f[u] >= INF_WEIGHT && dist_b[u] >= INF_WEIGHT) {
            touched.push_back(u);
        }
    }
};

static QueryWorkspace g_workspace;

// Hàm đệ quy giải nén các shortcut tiến (Forward)
static void unpackForwardEdge(const CHGraphQuery& graph, uint32_t u, uint32_t v, uint8_t mode, std::vector<uint32_t>& path_nodes) {
    // Tìm cạnh u -> v trong fwd_hot
    uint32_t start = graph.fwd_offset[u];
    uint32_t end = graph.fwd_offset[u + 1];
    
    uint32_t best_edge = NO_EDGE;
    float best_weight = INF_WEIGHT;

    for (uint32_t e = start; e < end; e++) {
        const auto& edge = graph.fwd_hot[e];
        if (edge.target == v && (edge.transport_mode & mode)) {
            if (edge.weight < best_weight) {
                best_weight = edge.weight;
                best_edge = e;
            }
        }
    }

    if (best_edge == NO_EDGE) {
        // Fallback an toàn, dù lý thuyết không bao giờ xảy ra nếu đồ thị đúng
        if (path_nodes.empty() || path_nodes.back() != v) {
            path_nodes.push_back(v);
        }
        return;
    }

    uint32_t mid = graph.fwd_cold[best_edge].mid_node;
    if (mid == NO_SHORTCUT) {
        // Cạnh gốc, không phải shortcut
        if (path_nodes.empty() || path_nodes.back() != v) {
            path_nodes.push_back(v);
        }
    } else {
        // Shortcut u -> w đi qua v (mid)
        unpackForwardEdge(graph, u, mid, mode, path_nodes);
        unpackForwardEdge(graph, mid, v, mode, path_nodes);
    }
}

// Hàm đệ quy giải nén các shortcut lùi (Backward)
// Lưu ý: Cạnh bwd w -> u nghĩa là có đường đi gốc u -> w
static void unpackBackwardEdge(const CHGraphQuery& graph, uint32_t w, uint32_t u, uint8_t mode, std::vector<uint32_t>& path_nodes) {
    // Tìm cạnh w -> u trong bwd_hot
    uint32_t start = graph.bwd_offset[w];
    uint32_t end = graph.bwd_offset[w + 1];
    
    uint32_t best_edge = NO_EDGE;
    float best_weight = INF_WEIGHT;

    for (uint32_t e = start; e < end; e++) {
        const auto& edge = graph.bwd_hot[e];
        if (edge.target == u && (edge.transport_mode & mode)) {
            if (edge.weight < best_weight) {
                best_weight = edge.weight;
                best_edge = e;
            }
        }
    }

    if (best_edge == NO_EDGE) {
        if (path_nodes.empty() || path_nodes.back() != w) {
            path_nodes.push_back(w);
        }
        return;
    }

    uint32_t mid = graph.bwd_cold[best_edge].mid_node;
    if (mid == NO_SHORTCUT) {
        // Cạnh gốc
        if (path_nodes.empty() || path_nodes.back() != w) {
            path_nodes.push_back(w);
        }
    } else {
        // Backward shortcut w -> u qua mid v
        // Nghĩa là chiều gốc: u -> v -> w
        // Nên unpack u -> v (nghĩa là backward v -> u) trước, sau đó v -> w (backward w -> v)
        unpackBackwardEdge(graph, mid, u, mode, path_nodes);
        unpackBackwardEdge(graph, w, mid, mode, path_nodes);
    }
}

RouteResult findShortestPathCH(const CHGraphQuery& graph, uint32_t source, uint32_t target, uint8_t transport_mode) {
    RouteResult result;
    if (source >= graph.num_nodes || target >= graph.num_nodes) return result;

    if (source == target) {
        result.valid = true;
        result.path_coords.push_back({graph.lat[source], graph.lon[source]});
        result.path_nodes.push_back(source);
        return result;
    }

    // Lazy init workspace (chỉ allocate lần đầu hoặc khi graph thay đổi kích thước)
    if (g_workspace.capacity < graph.num_nodes) {
        g_workspace.init(graph.num_nodes);
    }
    g_workspace.reset();  // Reset chỉ các node đã chạm từ query trước — O(visited)

    // Reference aliases để code bên dưới giữ nguyên cú pháp
    auto& dist_f   = g_workspace.dist_f;
    auto& dist_b   = g_workspace.dist_b;
    auto& parent_f = g_workspace.parent_f;
    auto& parent_b = g_workspace.parent_b;

    std::priority_queue<PQState, std::vector<PQState>, std::greater<PQState>> pq_f;
    std::priority_queue<PQState, std::vector<PQState>, std::greater<PQState>> pq_b;

    g_workspace.touch(source);
    dist_f[source] = 0.0f;
    pq_f.push({0.0f, source});

    g_workspace.touch(target);
    dist_b[target] = 0.0f;
    pq_b.push({0.0f, target});

    float best_weight = INF_WEIGHT;
    uint32_t meet_node = NO_EDGE;

    while (!pq_f.empty() || !pq_b.empty()) {
        // Điều kiện dừng: Khi tổng khoảng cách nhỏ nhất ở đỉnh PQ lớn hơn best_weight
        float min_f = pq_f.empty() ? INF_WEIGHT : pq_f.top().dist;
        float min_b = pq_b.empty() ? INF_WEIGHT : pq_b.top().dist;

        if (min_f + min_b >= best_weight) {
            break; // Gặp nhau và không còn khả năng tìm được đường ngắn hơn
        }

        // Ưu tiên duyệt queue nào có khoảng cách nhỏ hơn
        if (!pq_f.empty() && (pq_b.empty() || min_f <= min_b)) {
            auto [d, u] = pq_f.top();
            pq_f.pop();

            if (d > dist_f[u]) continue;

            // Kiểm tra giao nhau
            if (dist_b[u] != INF_WEIGHT && d + dist_b[u] < best_weight) {
                best_weight = d + dist_b[u];
                meet_node = u;
            }

            // Relax các cạnh forward (chỉ đi lên node có rank cao hơn)
            uint32_t start = graph.fwd_offset[u];
            uint32_t end = graph.fwd_offset[u + 1];
            for (uint32_t i = start; i < end; i++) {
                const auto& edge = graph.fwd_hot[i];
                if ((edge.transport_mode & transport_mode) == 0) continue;
                
                uint32_t v = edge.target;
                if (graph.rank[v] < graph.rank[u]) continue;

                float new_dist = d + edge.weight;
                if (new_dist < dist_f[v]) {
                    g_workspace.touch(v);
                    dist_f[v] = new_dist;
                    parent_f[v] = u;
                    pq_f.push({new_dist, v});
                }
            }
        } else {
            auto [d, u] = pq_b.top();
            pq_b.pop();

            if (d > dist_b[u]) continue;

            // Kiểm tra giao nhau
            if (dist_f[u] != INF_WEIGHT && d + dist_f[u] < best_weight) {
                best_weight = dist_f[u] + d;
                meet_node = u;
            }

            // Relax các cạnh backward (chỉ đi lên node có rank cao hơn)
            uint32_t start = graph.bwd_offset[u];
            uint32_t end = graph.bwd_offset[u + 1];
            for (uint32_t i = start; i < end; i++) {
                const auto& edge = graph.bwd_hot[i];
                if ((edge.transport_mode & transport_mode) == 0) continue;

                uint32_t v = edge.target;
                if (graph.rank[v] < graph.rank[u]) continue;

                float new_dist = d + edge.weight;
                if (new_dist < dist_b[v]) {
                    g_workspace.touch(v);
                    dist_b[v] = new_dist;
                    parent_b[v] = u;
                    pq_b.push({new_dist, v});
                }
            }
        }
    }

    if (meet_node == NO_EDGE) {
        return result; // Không tìm thấy đường
    }

    result.valid = true;
    result.time_s = best_weight; // Thời gian (do weight đang dùng time_s)

    // Unpack nửa forward (từ source đến meet_node)
    std::vector<uint32_t> fwd_path;
    uint32_t curr = meet_node;
    while (curr != source) {
        fwd_path.push_back(curr);
        curr = parent_f[curr];
    }
    fwd_path.push_back(source);
    std::reverse(fwd_path.begin(), fwd_path.end()); // source -> ... -> meet_node

    result.path_nodes.push_back(source);
    for (size_t i = 0; i + 1 < fwd_path.size(); i++) {
        unpackForwardEdge(graph, fwd_path[i], fwd_path[i+1], transport_mode, result.path_nodes);
    }

    // Unpack nửa backward (từ meet_node đến target)
    std::vector<uint32_t> bwd_path;
    curr = meet_node;
    while (curr != target) {
        bwd_path.push_back(curr);
        curr = parent_b[curr];
    }
    bwd_path.push_back(target);

    // bwd_path hiện đang là: meet_node -> ... -> target
    // Ta không cần reverse bwd_path vì nó ĐÃ đúng chiều từ meet_node đến target!
    // Tuy nhiên, để dùng chung logic loop i, i+1:
    
    for (size_t i = 0; i + 1 < bwd_path.size(); i++) {
        // Cạnh trong đồ thị backward từ v -> u nghĩa là gốc u -> v.
        // bwd_path[i] là u, bwd_path[i+1] là v. 
        // Backward search relax từ v lên u, tức là parent_b[u] = v.
        // Vậy nên cạnh backward là v -> u (tức bwd_path[i+1] -> bwd_path[i])
        unpackBackwardEdge(graph, bwd_path[i+1], bwd_path[i], transport_mode, result.path_nodes);
    }

    // Tính tổng distance (Haversine) và ghi tọa độ
    double total_dist = 0.0;
    for (size_t i = 0; i < result.path_nodes.size(); i++) {
        uint32_t node = result.path_nodes[i];
        result.path_coords.push_back({graph.lat[node], graph.lon[node]});
        
        if (i > 0) {
            uint32_t prev = result.path_nodes[i - 1];
            total_dist += haversine(graph.lat[prev], graph.lon[prev], graph.lat[node], graph.lon[node]);
        }
    }
    
    result.distance_m = total_dist;
    return result;
}
