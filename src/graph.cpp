#include "graph.h"
#include "haversine.h"
#include "osm_parser.h"
#include <iostream>
#include <algorithm>
#include <chrono>

void buildGraph(CHGraph& graph, const ParsedData& data) {
    auto t0 = std::chrono::steady_clock::now();
    std::cout << "=== Building Graph ===\n";

    uint32_t n = static_cast<uint32_t>(data.node_ids.size());
    std::cout << "  Initializing " << n << " nodes...\n";
    graph.resize(n);

    for (uint32_t i = 0; i < n; i++) {
        graph.lat[i] = data.node_coords[i].lat;
        graph.lon[i] = data.node_coords[i].lon;
        graph.osm_id[i] = data.node_ids[i];
    }
    std::cout << "  Mapped " << n << " nodes to compact indices\n";

    std::cout << "  Building edges from " << data.ways.size() << " ways...\n";
    size_t edge_count = 0;
    size_t skipped = 0;

    for (const auto& way : data.ways) {
        uint8_t mode = getTransportMode(way.highway_type);
        float speed_kmh = getSpeedKmh(way.highway_type);

        for (size_t i = 0; i + 1 < way.node_ids.size(); i++) {
            auto it_u = std::lower_bound(data.node_ids.begin(), data.node_ids.end(), way.node_ids[i]);
            auto it_v = std::lower_bound(data.node_ids.begin(), data.node_ids.end(), way.node_ids[i + 1]);

            if (it_u == data.node_ids.end() || *it_u != way.node_ids[i] ||
                it_v == data.node_ids.end() || *it_v != way.node_ids[i + 1]) {
                skipped++;
                continue;
            }

            uint32_t u = static_cast<uint32_t>(std::distance(data.node_ids.begin(), it_u));
            uint32_t v = static_cast<uint32_t>(std::distance(data.node_ids.begin(), it_v));
            
            if (u == v) continue;

            double dist_m = haversine(graph.lat[u], graph.lon[u],
                                      graph.lat[v], graph.lon[v]);
            float time_s = static_cast<float>(dist_m * 3.6 / speed_kmh);

            // Forward edge u -> v
            graph.addFwdEdge(u, v, time_s, NO_SHORTCUT, mode);
            graph.addBwdEdge(v, u, time_s, NO_SHORTCUT, mode);
            edge_count++;

            // Reverse edge if not oneway
            if (!way.oneway) {
                graph.addFwdEdge(v, u, time_s, NO_SHORTCUT, mode);
                graph.addBwdEdge(u, v, time_s, NO_SHORTCUT, mode);
                edge_count++;
            }
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    std::cout << "  Created " << edge_count << " edges (" << skipped << " skipped)\n";
    std::cout << "  Total forward edges: " << graph.countEdges() << "\n";
    std::cout << "  Edge Pool capacity: " << graph.fwd_edges.capacity() << " (fwd) / " << graph.bwd_edges.capacity() << " (bwd)\n";
    std::cout << "=== Graph built in " << elapsed << " seconds ===\n\n";
}
