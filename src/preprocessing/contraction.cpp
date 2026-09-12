#include "contraction.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <limits>
#include <queue>
#include <vector>

static constexpr float INF_WEIGHT = std::numeric_limits<float>::max();
static constexpr int WITNESS_HOP_LIMIT = 5;      
static constexpr int WITNESS_SETTLE_LIMIT = 500;  

static bool witnessSearch(
    const CHGraph& graph,
    uint32_t source,
    uint32_t target,
    float max_cost,
    uint32_t excluded_node,
    uint8_t mode_filter,
    std::vector<float>& dist,
    std::vector<uint32_t>& dist_gen,
    uint32_t& current_gen)
{
    current_gen++;

    std::priority_queue<
        std::pair<float, uint32_t>,
        std::vector<std::pair<float, uint32_t>>,
        std::greater<std::pair<float, uint32_t>>
    > pq;

    dist[source] = 0.0f;
    dist_gen[source] = current_gen;
    pq.push({0.0f, source});

    int settled = 0;

    while (!pq.empty() && settled < WITNESS_SETTLE_LIMIT) {
        auto [d, u] = pq.top();
        pq.pop();

        if (dist_gen[u] != current_gen || d > dist[u]) continue;
        if (d > max_cost) break;  

        if (u == target) return true;  

        settled++;

        for (uint32_t e_idx = graph.first_fwd[u]; e_idx != NO_EDGE; e_idx = graph.fwd_edges[e_idx].next) {
            const auto& e = graph.fwd_edges[e_idx];
            if (graph.contracted[e.target]) continue;
            if (e.target == excluded_node) continue;
            if ((e.transport_mode & mode_filter) == 0) continue;

            float new_dist = d + e.weight;
            if (new_dist > max_cost) continue;

            if (dist_gen[e.target] != current_gen || new_dist < dist[e.target]) {
                dist[e.target] = new_dist;
                dist_gen[e.target] = current_gen;
                pq.push({new_dist, e.target});
            }
        }
    }

    return false;  
}

struct ContractionResult {
    int edge_difference;       
    int contracted_neighbors;  
    std::vector<CHEdge> shortcuts;  
};

static ContractionResult simulateContraction(
    const CHGraph& graph,
    uint32_t v,
    uint8_t mode_filter,
    std::vector<float>& dist_buf,
    std::vector<uint32_t>& gen_buf,
    uint32_t& gen_counter)
{
    ContractionResult result;
    result.edge_difference = 0;
    result.contracted_neighbors = 0;

    int edges_of_v = 0;
    for (uint32_t e_idx = graph.first_fwd[v]; e_idx != NO_EDGE; e_idx = graph.fwd_edges[e_idx].next) {
        const auto& e = graph.fwd_edges[e_idx];
        if (!graph.contracted[e.target] && (e.transport_mode & mode_filter)) edges_of_v++;
        if (graph.contracted[e.target]) result.contracted_neighbors++;
    }
    for (uint32_t e_idx = graph.first_bwd[v]; e_idx != NO_EDGE; e_idx = graph.bwd_edges[e_idx].next) {
        const auto& e = graph.bwd_edges[e_idx];
        if (!graph.contracted[e.target] && (e.transport_mode & mode_filter)) edges_of_v++;
        if (graph.contracted[e.target]) result.contracted_neighbors++;
    }

    for (uint32_t in_idx = graph.first_bwd[v]; in_idx != NO_EDGE; in_idx = graph.bwd_edges[in_idx].next) {
        const auto& e_in = graph.bwd_edges[in_idx];
        if (graph.contracted[e_in.target]) continue;
        if ((e_in.transport_mode & mode_filter) == 0) continue;
        
        uint32_t u = e_in.target;
        float w_uv = e_in.weight;

        for (uint32_t out_idx = graph.first_fwd[v]; out_idx != NO_EDGE; out_idx = graph.fwd_edges[out_idx].next) {
            const auto& e_out = graph.fwd_edges[out_idx];
            if (graph.contracted[e_out.target]) continue;
            if ((e_out.transport_mode & mode_filter) == 0) continue;
            
            uint32_t w = e_out.target;
            if (u == w) continue;
            
            float w_vw = e_out.weight;
            float shortcut_cost = w_uv + w_vw;

            if (!witnessSearch(graph, u, w, shortcut_cost, v, mode_filter,
                              dist_buf, gen_buf, gen_counter)) {
                uint8_t sc_mode = e_in.transport_mode & e_out.transport_mode & mode_filter;
                if (sc_mode == 0) sc_mode = mode_filter;  

                // Note: 'next' pointer doesn't matter here since we are just returning it in a vector
                result.shortcuts.push_back({w, shortcut_cost, v, sc_mode, NO_EDGE});
            }
        }
    }

    result.edge_difference = static_cast<int>(result.shortcuts.size()) - edges_of_v;
    return result;
}

static int calculatePriority(const ContractionResult& cr) {
    return cr.edge_difference + cr.contracted_neighbors * 2;
}

void runContraction(CHGraph& graph, uint8_t transport_mode_filter) {
    auto t0 = std::chrono::steady_clock::now();
    std::cout << "=== Running Contraction Hierarchies ===\n";
    std::cout << "  Nodes: " << graph.num_nodes << "\n";
    std::cout << "  Initial edges: " << graph.countEdges() << "\n";

    uint32_t active_nodes = 0;
    for (uint32_t i = 0; i < graph.num_nodes; i++) {
        bool has_edge = false;
        for (uint32_t e_idx = graph.first_fwd[i]; e_idx != NO_EDGE; e_idx = graph.fwd_edges[e_idx].next) {
            if (graph.fwd_edges[e_idx].transport_mode & transport_mode_filter) { has_edge = true; break; }
        }
        if (!has_edge) {
            for (uint32_t e_idx = graph.first_bwd[i]; e_idx != NO_EDGE; e_idx = graph.bwd_edges[e_idx].next) {
                if (graph.bwd_edges[e_idx].transport_mode & transport_mode_filter) { has_edge = true; break; }
            }
        }
        if (has_edge) active_nodes++;
        else {
            graph.contracted[i] = true;
            graph.rank[i] = 0;
        }
    }
    std::cout << "  Active nodes (with matching edges): " << active_nodes << "\n";

    std::vector<float> dist_buf(graph.num_nodes, INF_WEIGHT);
    std::vector<uint32_t> gen_buf(graph.num_nodes, 0);
    uint32_t gen_counter = 0;

    using PQEntry = std::pair<int, uint32_t>;
    std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> pq;

    std::cout << "  Computing initial priorities...\n";
    for (uint32_t v = 0; v < graph.num_nodes; v++) {
        if (graph.contracted[v]) continue;

        auto cr = simulateContraction(graph, v, transport_mode_filter,
                                       dist_buf, gen_buf, gen_counter);
        int pri = calculatePriority(cr);
        pq.push({pri, v});
    }

    int32_t current_rank = 1; 
    size_t total_shortcuts = 0;
    auto last_report = std::chrono::steady_clock::now();

    while (!pq.empty()) {
        auto [pri, v] = pq.top();
        pq.pop();

        if (graph.contracted[v]) continue;

        auto cr = simulateContraction(graph, v, transport_mode_filter,
                                       dist_buf, gen_buf, gen_counter);
        int new_pri = calculatePriority(cr);

        if (new_pri > pri && !pq.empty() && new_pri > pq.top().first) {
            pq.push({new_pri, v});
            continue;
        }

        graph.contracted[v] = true;
        graph.rank[v] = current_rank;

        // Properly generate and add shortcuts
        for (uint32_t in_idx = graph.first_bwd[v]; in_idx != NO_EDGE; in_idx = graph.bwd_edges[in_idx].next) {
            const auto& e_in = graph.bwd_edges[in_idx];
            if (graph.contracted[e_in.target]) continue;
            if ((e_in.transport_mode & transport_mode_filter) == 0) continue;
            
            uint32_t u = e_in.target;
            float w_uv = e_in.weight;

            for (uint32_t out_idx = graph.first_fwd[v]; out_idx != NO_EDGE; out_idx = graph.fwd_edges[out_idx].next) {
                const auto& e_out = graph.fwd_edges[out_idx];
                if (graph.contracted[e_out.target]) continue;
                if ((e_out.transport_mode & transport_mode_filter) == 0) continue;
                
                uint32_t w = e_out.target;
                if (u == w) continue;
                float w_vw = e_out.weight;
                float shortcut_cost = w_uv + w_vw;

                if (!witnessSearch(graph, u, w, shortcut_cost, v, transport_mode_filter,
                                  dist_buf, gen_buf, gen_counter)) {
                    uint8_t sc_mode = e_in.transport_mode & e_out.transport_mode & transport_mode_filter;
                    if (sc_mode == 0) sc_mode = transport_mode_filter;

                    // Add forward shortcut u -> w
                    graph.addFwdEdge(u, w, shortcut_cost, v, sc_mode);
                    // Add backward shortcut w -> u (reverse)
                    graph.addBwdEdge(w, u, shortcut_cost, v, sc_mode);
                    
                    total_shortcuts++;
                }
            }
        }

        current_rank++;

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - last_report).count() >= 5.0) {
            double pct = 100.0 * (current_rank - 1) / active_nodes;
            std::printf("  Contracted %d / %u nodes (%.1f%%), shortcuts: %zu\n",
                       current_rank - 1, active_nodes, pct, total_shortcuts);
            last_report = now;
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    std::cout << "  Total contracted: " << (current_rank - 1) << " nodes\n";
    std::cout << "  Total shortcuts generated: " << total_shortcuts << "\n";
    std::cout << "  Final fwd edge count: " << graph.countEdges() << "\n";
    std::cout << "=== Contraction Hierarchies completed in " << elapsed << " seconds ===\n\n";
}
