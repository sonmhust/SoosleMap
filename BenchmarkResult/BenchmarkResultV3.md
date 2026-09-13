# C++ Backend Benchmark Results V3

### 1. Data Loading (Zero-parse / Binary fread)
| Task | File | RAM Usage | Execution Time |
| :--- | :--- | :--- | :--- |
| Load STR-Tree | `snap_tree.bin` | ~362 MB | 209.58 ms |
| Load CH Graph | `hanoi_ch.bin` | ~226 MB | 303.12 ms |

### 2. Snap-to-Edge Query (STR-Tree BFS)
| Task | Action / Location | Execution Time |
| :--- | :--- | :--- |
| Snap 1 (Cold) | Hoan Kiem Lake | 2.328 ms |
| Snap 2 (Warm) | Hanoi University of Science and Technology | 2.063 ms |

### 3. Routing Query (Contraction Hierarchies)
| Task | V2 (Before) | V3 (After) | Speedup |
| :--- | :--- | :--- | :--- |
| Route 1 (Cold) | 8.17 ms | 10.325 ms | ~1x |
| Route 2 (Warm) | 8.88 ms | 0.113 ms | ~80x |
| Route 3 (Same) | — | 0.005 ms | — |

### 4. Full Pipeline (Snap + Route)
| Stage | Execution Time |
| :--- | :--- |
| Snap from | ~2.3 ms |
| Snap to | ~2.1 ms |
| Route (warm) | ~0.1 ms |
| Total | ~4.5 ms |
