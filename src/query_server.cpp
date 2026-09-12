#define NOMINMAX
#include <iostream>
#include <sstream>
#include <string>
#include <chrono>
#include <iomanip>

#include "snap_tree.h"
#include "serializer.h"
#include "graph.h"
#include "haversine.h"
#include "geocoding_db.h"


//file này để nạp file .bin vào RAM và thực hiện truy vấn

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <snap_tree.bin> <ch_graph.bin>\n";
        std::cerr << "Example: " << argv[0] << " data/snap_tree.bin data/vietnam_ch.bin\n";
        return 1;
    }

    std::string snap_path = argv[1];
    std::string ch_path   = argv[2];

    
    // Load STR-Tree
    std::cout << "Loading STR-Tree from " << snap_path << " ...\n";
    auto t0 = std::chrono::steady_clock::now();
    SnapTree snap_tree;
    if (!loadSnapTree(snap_tree, snap_path)) {
        std::cerr << "FATAL: Cannot load snap_tree from " << snap_path << "\n";
        return 1;
    }
    double load_snap_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    std::cout << "  STR-Tree nodes : " << snap_tree.nodes.size() << "\n";
    std::cout << "  Graph edges    : " << snap_tree.num_graph_edges << "\n";
    std::cout << "  Load time      : " << std::fixed << std::setprecision(1)
              << load_snap_ms << " ms\n\n";

    // Load CH Graph (cần lat/lon của nodes để chiếu điểm)
    std::cout << "Loading CH Graph from " << ch_path << " ...\n";
    t0 = std::chrono::steady_clock::now();
    CHGraphQuery graph;
    if (!loadGraphQuery(graph, ch_path)) {
        std::cerr << "FATAL: Cannot load graph from " << ch_path << "\n";
        return 1;
    }
    double load_graph_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    std::cout << "  Graph nodes    : " << graph.num_nodes << "\n";
    std::cout << "  Load time      : " << load_graph_ms << " ms\n\n";

    // Interactive query loop
    std::cout << "============================================\n";
    std::cout << "  SNAP-TO-EDGE QUERY SERVER (demo CLI)\n";
    std::cout << "============================================\n";
    std::cout << "Enter lat lon (or 'q' to quit):\n\n";

    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) break;
        if (line == "q" || line == "quit") break;
        if (line.empty()) continue;

        double qlat, qlon;
        try {
            // Thay dấu phẩy bằng space để parse linh hoạt
            for (char& c : line) if (c == ',') c = ' ';
            std::istringstream iss(line);
            if (!(iss >> qlat >> qlon)) throw std::invalid_argument("bad input");
        } catch (...) {
            std::cout << "  [Invalid] Nhập theo dạng: 21.0278 105.8342\n\n";
            continue;
        }

        // Snap
        auto snap_t0 = std::chrono::steady_clock::now();
        SnapResult result = snapNearestQuery(snap_tree, graph, qlat, qlon);
        double snap_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - snap_t0).count();

        if (!result.valid) {
            std::cout << "  [No result] Không tìm thấy cạnh nào.\n\n";
            continue;
        }

        // Tọa độ của u và v (để hiển thị)
        double ulat = graph.lat[result.edge_u];
        double ulon = graph.lon[result.edge_u];
        double vlat = graph.lat[result.edge_v];
        double vlon = graph.lon[result.edge_v];
        double edge_len = haversine(ulat, ulon, vlat, vlon);

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "  Query          : (" << qlat << ", " << qlon << ")\n";
        std::cout << "  Nearest edge   : node " << result.edge_u
                  << " → node " << result.edge_v << "\n";
        std::cout << "  Node U coords  : (" << ulat << ", " << ulon << ")\n";
        std::cout << "  Node V coords  : (" << vlat << ", " << vlon << ")\n";
        std::cout << "  Edge length    : " << std::setprecision(1)
                  << edge_len << " m\n";
        std::cout << "  t parameter    : " << std::setprecision(4) << result.t
                  << "  (0=at U, 1=at V)\n";
        std::cout << "  Projected P    : (" << std::setprecision(6)
                  << result.proj_lat << ", " << result.proj_lon << ")\n";
        std::cout << "  Distance P→Q   : " << std::setprecision(2)
                  << result.dist_m << " m\n";
        std::cout << "  Snap latency   : " << std::setprecision(3)
                  << snap_ms << " ms\n\n";
    }

    return 0;
}
