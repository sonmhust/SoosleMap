#define NOMINMAX
#include <iostream>
#include <string>
#include <filesystem>
#include <chrono>

#include "osm_parser.h"
#include "geocoding_db.h"
#include "graph.h"
#include "contraction.h"
#include "serializer.h"
#include "snap_tree.h"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    std::string input_file = (argc >= 2) ? argv[1] : "hanoi.osm.pbf";
    std::string data_dir   = "data";

    fs::create_directories(data_dir);
    std::string db_path   = data_dir + "/database.sqlite";
    std::string ch_path   = data_dir + "/hanoi_ch.bin";
    std::string snap_path = data_dir + "/snap_tree.bin";

    auto total_start = std::chrono::steady_clock::now();

    std::cout << "============================================\n";
    std::cout << "  ROUTING ENGINE - OFFLINE PREPROCESSING\n";
    std::cout << "============================================\n";
    std::cout << "Input:  " << input_file                          << "\n";
    std::cout << "Output: " << fs::absolute(data_dir).string()    << "\n\n";

    try {
        // ===================================================
        // Step 1: Initialize Geocoding DB
        // ===================================================
        std::cout << "=== Step 1: Initializing Geocoding Database ===\n";
        fs::remove(db_path);   // Remove stale DB
        GeocodingDB geo_db(db_path);
        geo_db.beginTransaction();
        std::cout << "  DB path: " << db_path << "\n\n";

        // ===================================================
        // Step 2: Parse OSM (2-pass: ways + nodes + POIs)
        // ===================================================
        std::cout << "=== Step 2: Parsing OSM Data ===\n";
        ParsedData parsed = parseOSM(input_file, &geo_db);

        geo_db.commitTransaction();
        geo_db.finalizeFTS();
        std::cout << "  Geocoding DB: " << geo_db.getInsertedCount() << " POIs\n\n";

        // ===================================================
        // Step 3: Build Graph (Edge Pool)
        // ===================================================
        std::cout << "=== Step 3: Building Graph ===\n";
        CHGraph graph;
        buildGraph(graph, parsed);

        // Free parsed memory before CH (avoid RAM spike)
        parsed.node_ids.clear();    parsed.node_ids.shrink_to_fit();
        parsed.node_coords.clear(); parsed.node_coords.shrink_to_fit();
        parsed.ways.clear();        parsed.ways.shrink_to_fit();
        std::cout << "  Freed parsed data before CH\n\n";

        // ===================================================
        // Step 4: Contraction Hierarchies
        // ===================================================
        std::cout << "=== Step 4: Running Contraction Hierarchies ===\n";
        uint8_t vehicle_mode = MODE_CAR | MODE_MOTORBIKE;
        runContraction(graph, vehicle_mode);

        // ===================================================
        // Step 5: Serialize → SoA v2 directly (no converter)
        // ===================================================
        std::cout << "=== Step 5: Serializing Graph ===\n";
        serializeGraph(graph, ch_path);

        // ===================================================
        // Step 6: Build STR-Tree (Snap-to-Edge)
        // ===================================================
        std::cout << "=== Step 6: Building STR-Tree ===\n";
        auto snap_start = std::chrono::steady_clock::now();
        SnapTree snap_tree = buildSnapTree(graph);
        saveSnapTree(snap_tree, snap_path);
        double snap_sec = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - snap_start).count();
        std::cout << "  Edges indexed : " << snap_tree.num_graph_edges << "\n";
        std::cout << "  STR nodes     : " << snap_tree.nodes.size()    << "\n";
        std::cout << "  Build time    : " << snap_sec << " s\n\n";

        // ===================================================
        // Summary
        // ===================================================
        double total_sec = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - total_start).count();

        std::cout << "============================================\n";
        std::cout << "  PREPROCESSING COMPLETE\n";
        std::cout << "============================================\n";
        std::cout << "  Total time: " << total_sec << " s\n";
        std::cout << "  Files:\n";
        std::cout << "    " << fs::absolute(db_path).string()   << "\n";
        std::cout << "    " << fs::absolute(ch_path).string()   << "\n";
        std::cout << "    " << fs::absolute(snap_path).string() << "\n";
        std::cout << "  Graph:\n";
        std::cout << "    Nodes:     " << graph.num_nodes      << "\n";
        std::cout << "    Edges:     " << graph.countEdges()   << "\n";
        std::cout << "    Shortcuts: " << graph.countShortcuts()<< "\n";
        std::cout << "============================================\n";

    } catch (const std::exception& e) {
        std::cerr << "\nFATAL ERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
