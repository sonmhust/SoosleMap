# C++ Backend Benchmark Results (Hanoi Dataset)

### 1. Data Loading (Zero-parse / Binary fread)
| Task | File | RAM Usage | Execution Time |
| :--- | :--- | :--- | :--- |
| Load STR-Tree | `snap_tree.bin` | ~362 MB | 209.58 ms |
| Load CH Graph | `hanoi_ch.bin` | ~226 MB | 303.12 ms |

### 2. Snap-to-Edge Query (STR-Tree BFS)
| Task | Action / Location | Execution Time |
| :--- | :--- | :--- |
| Snap 1 (Cold) | Point 1: Bach Mai Hospital | 2.55 ms |
| Snap 2 (Warm) | Point 2: Hoan Kiem Lake | 1.85 ms |

### 3. Routing Query (Contraction Hierarchies)
| Task | Execution Time | Routing Result |
| :--- | :--- | :--- |
| Route 1 (Cold) | 8.17 ms | Distance: 2.72 km |
| Route 2 (Cached) | 8.88 ms | Travel Time: 2.86 minutes |
