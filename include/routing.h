#pragma once
#include "graph.h"
#include "snap_tree.h"
#include <vector>

// Kết quả trả về của thuật toán tìm đường
struct RouteResult {
    bool valid = false;                                 // True nếu tìm thấy đường
    double distance_m = 0.0;                            // Khoảng cách theo mét (tùy thuộc vào weight, hiện tại weight là time_s)
    double time_s = 0.0;                                // Thời gian di chuyển (giây)
    std::vector<std::pair<double, double>> path_coords; // Mảng tọa độ (lat, lon) để vẽ lên bản đồ
    std::vector<uint32_t> path_nodes;                   // Mảng ID của các node (tùy chọn)
};

// Hàm tìm đường ngắn nhất trên đồ thị CH (Bi-directional Dijkstra)
// mode: Dùng mask TransportMode (VD: MODE_CAR, MODE_MOTORBIKE)
RouteResult findShortestPathCH(const CHGraphQuery& graph, uint32_t source, uint32_t target, uint8_t transport_mode);

// Snap-to-edge với LRU cache 64 entry.
// Drop-in replacement cho snapNearestQuery() — bỏ qua STR-Tree BFS nếu điểm GPS đã có trong cache.
// Key: tọa độ được round đến 5 chữ số thập phân (~1.1m precision).
SnapResult snapNearestCached(const SnapTree& tree, const CHGraphQuery& graph,
                              double lat, double lon);
