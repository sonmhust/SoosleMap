// serializer.cpp — CH Graph binary I/O (SoA CSR format, v2)

// Cross-platform large file seek/tell
#ifdef _WIN32
  #define fseek64 _fseeki64
  #define ftell64 _ftelli64
#else
  #define fseek64 fseeko64
  #define ftell64 ftello64
#endif
//
// File layout (v1):
//   [FileHeader]    20 bytes   — magic 0x43484752 ("CHGR"), num_nodes, num_fwd, num_bwd, version=1
//   [SerNodeHot]   (N+1) * 12 bytes — rank/fwd_offset/bwd_offset  (+sentinel at [N])
//   [SerNodeCold]    N   * 16 bytes — lat/lon  (no sentinel needed)
//   [FwdHot]       num_fwd * 12 bytes — target/weight/transport_mode
//   [FwdCold]      num_fwd *  4 bytes — mid_node
//   [BwdHot]       num_bwd * 12 bytes
//   [BwdCold]      num_bwd *  4 bytes
//
// Hot/Cold split rationale:
//   SerNodeHot  12B: rank + CSR offsets — 5.3 nodes/cache-line (vs 2.3 with old 28B SerNode)
//   SerNodeCold 16B: lat/lon — 4 nodes/cache-line, no sentinel needed

#include "serializer.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <iostream>

static constexpr uint32_t MAGIC   = 0x43484752;  // "CHGR"
static constexpr uint32_t VERSION = 1;

#pragma pack(push, 1)

struct FileHeader {
    uint32_t magic;
    uint32_t num_nodes;
    uint32_t num_fwd_edges;
    uint32_t num_bwd_edges;
    uint32_t version;    // = 1
};  // = 20 bytes

// HOT: read every Dijkstra relaxation — 5.3 nodes per 64B cache line
struct SerNodeHot {
    int32_t  rank;        // 4B
    uint32_t fwd_offset;  // 4B
    uint32_t bwd_offset;  // 4B
};  // = 12B

// COLD: read only on snap / path unpack — 4 nodes per 64B cache line
struct SerNodeCold {
    double lat;  // 8B
    double lon;  // 8B
};  // = 16B

struct SerHotEdge {
    uint32_t target;
    float    weight;
    uint8_t  transport_mode;
    uint8_t  padding[3];
};  // = 12 bytes

struct SerColdEdge {
    uint32_t mid_node;
};  // = 4 bytes

#pragma pack(pop)

// ============================================================
// serializeGraph — write SoA CSR binary
// ============================================================
void serializeGraph(const CHGraph& graph, const std::string& filepath) {
    // v1 format: SerNodeHot(N+1) then SerNodeCold(N)
    auto t0 = std::chrono::steady_clock::now();

    uint32_t n       = graph.num_nodes;
    uint32_t num_fwd = static_cast<uint32_t>(graph.fwd_edges.size());
    uint32_t num_bwd = static_cast<uint32_t>(graph.bwd_edges.size());

    std::cout << "=== Serializing Graph ===\n";
    std::cout << "  Nodes:          " << n       << "\n";
    std::cout << "  Forward edges:  " << num_fwd << "\n";
    std::cout << "  Backward edges: " << num_bwd << "\n";

    FILE* f = fopen(filepath.c_str(), "wb");
    if (!f) {
        std::cerr << "  ERROR: Cannot open " << filepath << " for writing\n";
        return;
    }

    // ── Header ────────────────────────────────────────────────
    FileHeader hdr;
    hdr.magic         = MAGIC;
    hdr.num_nodes     = n;
    hdr.num_fwd_edges = num_fwd;
    hdr.num_bwd_edges = num_bwd;
    hdr.version       = VERSION;
    fwrite(&hdr, sizeof(hdr), 1, f);

    // ── SerNodeHot (N+1): CSR offsets + sentinel ──────────────
    {
        std::vector<SerNodeHot> hot(n + 1);
        uint32_t fwd_off = 0, bwd_off = 0;
        for (uint32_t i = 0; i < n; i++) {
            hot[i] = {graph.rank[i], fwd_off, bwd_off};
            for (uint32_t e = graph.first_fwd[i]; e != NO_EDGE; e = graph.fwd_edges[e].next) fwd_off++;
            for (uint32_t e = graph.first_bwd[i]; e != NO_EDGE; e = graph.bwd_edges[e].next) bwd_off++;
        }
        hot[n] = {-1, fwd_off, bwd_off};  // sentinel
        fwrite(hot.data(), sizeof(SerNodeHot), n + 1, f);
    }

    // ── SerNodeCold (N): lat/lon, no sentinel needed ──────────
    {
        std::vector<SerNodeCold> cold(n);
        for (uint32_t i = 0; i < n; i++)
            cold[i] = {graph.lat[i], graph.lon[i]};
        fwrite(cold.data(), sizeof(SerNodeCold), n, f);
    }

    // Forward edges: Hot block then Cold block
    {
        std::vector<SerHotEdge>  hot(num_fwd);
        std::vector<SerColdEdge> cold(num_fwd);
        uint32_t idx = 0;
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t e = graph.first_fwd[i]; e != NO_EDGE; e = graph.fwd_edges[e].next) {
                const auto& ed = graph.fwd_edges[e];
                hot[idx]  = {ed.target, ed.weight, ed.transport_mode, {0,0,0}};
                cold[idx] = {ed.mid_node};
                idx++;
            }
        fwrite(hot.data(),  sizeof(SerHotEdge),  num_fwd, f);
        fwrite(cold.data(), sizeof(SerColdEdge), num_fwd, f);
    }

    // Backward edges: Hot block then Cold block
    {
        std::vector<SerHotEdge>  hot(num_bwd);
        std::vector<SerColdEdge> cold(num_bwd);
        uint32_t idx = 0;
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t e = graph.first_bwd[i]; e != NO_EDGE; e = graph.bwd_edges[e].next) {
                const auto& ed = graph.bwd_edges[e];
                hot[idx]  = {ed.target, ed.weight, ed.transport_mode, {0,0,0}};
                cold[idx] = {ed.mid_node};
                idx++;
            }
        fwrite(hot.data(),  sizeof(SerHotEdge),  num_bwd, f);
        fwrite(cold.data(), sizeof(SerColdEdge), num_bwd, f);
    }

    fclose(f);

    FILE* fc = fopen(filepath.c_str(), "rb");
    if (fc) {
        fseek64(fc, 0, SEEK_END);
        long long sz = ftell64(fc);
        fclose(fc);
        std::cout << "  File size: " << (sz / (1024 * 1024)) << " MB\n";
    }
    std::cout << "  Output: " << filepath << "\n";
    std::cout << "=== Serialization done in "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()
              << " s ===\n\n";
}

// ============================================================
// loadGraphQuery — read binary into CHGraphQuery (online server)
// ============================================================
bool loadGraphQuery(CHGraphQuery& graph, const std::string& filepath) {
    auto t0 = std::chrono::steady_clock::now();
    std::cout << "=== Loading Graph ===\n";

    FILE* f = fopen(filepath.c_str(), "rb");
    if (!f) {
        std::cerr << "  ERROR: Cannot open " << filepath << "\n";
        return false;
    }

    FileHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) { fclose(f); return false; }
    if (hdr.magic != MAGIC) {
        std::cerr << "  ERROR: Invalid magic (got 0x" << std::hex << hdr.magic << ")\n";
        fclose(f); return false;
    }
    if (hdr.version != VERSION) {
        std::cerr << "  ERROR: Unsupported version " << std::dec << hdr.version
                  << " (expected " << VERSION << "). Rebuild hanoi_ch.bin.\n";
        fclose(f); return false;
    }

    uint32_t n = hdr.num_nodes;
    graph.num_nodes = n;
    graph.rank.resize(n);
    graph.fwd_offset.resize(n + 1);
    graph.bwd_offset.resize(n + 1);
    graph.lat.resize(n);
    graph.lon.resize(n);

    std::cout << "  Nodes:          " << n                 << "\n";
    std::cout << "  Forward edges:  " << hdr.num_fwd_edges << "\n";
    std::cout << "  Backward edges: " << hdr.num_bwd_edges << "\n";

    // ── SerNodeHot (N+1) ─────────────────────────────────────
    {
        std::vector<SerNodeHot> hot(n + 1);
        if (fread(hot.data(), sizeof(SerNodeHot), n + 1, f) != n + 1) { fclose(f); return false; }
        for (uint32_t i = 0; i < n; i++) {
            graph.rank[i]       = hot[i].rank;
            graph.fwd_offset[i] = hot[i].fwd_offset;
            graph.bwd_offset[i] = hot[i].bwd_offset;
        }
        graph.fwd_offset[n] = hot[n].fwd_offset;  // sentinel → CSR edge count of last node
        graph.bwd_offset[n] = hot[n].bwd_offset;
    }  // hot buffer freed here

    // ── SerNodeCold (N) ───────────────────────────────────────
    {
        std::vector<SerNodeCold> cold(n);
        if (fread(cold.data(), sizeof(SerNodeCold), n, f) != n) { fclose(f); return false; }
        for (uint32_t i = 0; i < n; i++) {
            graph.lat[i] = cold[i].lat;
            graph.lon[i] = cold[i].lon;
        }
    }  // cold buffer freed here

    auto read_hot = [&](std::vector<HotEdge>& out, uint32_t count) -> bool {
        out.resize(count);
        if (count == 0) return true;
        std::vector<SerHotEdge> tmp(count);
        if (fread(tmp.data(), sizeof(SerHotEdge), count, f) != count) return false;
        for (uint32_t i = 0; i < count; i++) {
            out[i].target         = tmp[i].target;
            out[i].weight         = tmp[i].weight;
            out[i].transport_mode = tmp[i].transport_mode;
            out[i].padding[0] = out[i].padding[1] = out[i].padding[2] = 0;
        }
        return true;
    };
    auto read_cold = [&](std::vector<ColdEdge>& out, uint32_t count) -> bool {
        out.resize(count);
        if (count == 0) return true;
        std::vector<SerColdEdge> tmp(count);
        if (fread(tmp.data(), sizeof(SerColdEdge), count, f) != count) return false;
        for (uint32_t i = 0; i < count; i++) out[i].mid_node = tmp[i].mid_node;
        return true;
    };

    if (!read_hot(graph.fwd_hot,   hdr.num_fwd_edges) ||
        !read_cold(graph.fwd_cold, hdr.num_fwd_edges) ||
        !read_hot(graph.bwd_hot,   hdr.num_bwd_edges) ||
        !read_cold(graph.bwd_cold, hdr.num_bwd_edges)) {
        fclose(f); return false;
    }

    fclose(f);

    size_t hot_mb  = (size_t)(hdr.num_fwd_edges + hdr.num_bwd_edges) * sizeof(HotEdge)  / (1024*1024);
    size_t cold_mb = (size_t)(hdr.num_fwd_edges + hdr.num_bwd_edges) * sizeof(ColdEdge) / (1024*1024);
    std::cout << "  Hot RAM:  " << hot_mb  << " MB\n";
    std::cout << "  Cold RAM: " << cold_mb << " MB\n";
    std::cout << "=== Graph loaded in "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()
              << " s ===\n\n";
    return true;
}

// ============================================================
// loadGraphCoordinates — load only lat/lon (lightweight)
// ============================================================
bool loadGraphCoordinates(CHGraphQuery& graph, const std::string& filepath) {
    FILE* f = fopen(filepath.c_str(), "rb");
    if (!f) return false;

    FileHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1 || hdr.magic != MAGIC) { fclose(f); return false; }
    if (hdr.version != VERSION) { fclose(f); return false; }

    uint32_t n = hdr.num_nodes;
    graph.num_nodes = n;
    graph.lat.resize(n);
    graph.lon.resize(n);

    // Skip SerNodeHot block (N+1 entries) to reach SerNodeCold
    long long hot_block_bytes = (long long)(n + 1) * sizeof(SerNodeHot);
    if (fseek64(f, hot_block_bytes, SEEK_CUR) != 0) { fclose(f); return false; }

    // Read SerNodeCold directly
    std::vector<SerNodeCold> cold(n);
    if (fread(cold.data(), sizeof(SerNodeCold), n, f) != n) { fclose(f); return false; }
    for (uint32_t i = 0; i < n; i++) {
        graph.lat[i] = cold[i].lat;
        graph.lon[i] = cold[i].lon;
    }

    fclose(f);
    std::cout << "  Loaded " << n << " node coordinates.\n";
    return true;
}

// Stub — format v1 AoS is gone
bool loadGraph(CHGraph& /*graph*/, const std::string& /*filepath*/) {
    std::cerr << "loadGraph: AoS format removed, use loadGraphQuery.\n";
    return false;
}
