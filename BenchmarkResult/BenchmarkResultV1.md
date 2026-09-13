# C++ Backend Benchmark Results (Hanoi Dataset)

### 1. Data Loading (Zero-parse / Binary fread)
| Task | File | RAM Usage | Execution Time |
| :--- | :--- | :--- | :--- |
| Load STR-Tree | `snap_tree.bin` | ~362 MB | 215.96 ms |
| Load CH Graph | `hanoi_ch.bin` | ~226 MB | 328.10 ms |

### 2. Snap-to-Edge Query (STR-Tree BFS)
| Task | GPS Coordinates | Nearest Node | Execution Time |
| :--- | :--- | :--- | :--- |
| Snap 1 (Cold) | Bach Mai Hospital (21.001, 105.841) | `149292` | 2364.07 ms |
| Snap 2 (Warm) | Hoan Kiem Lake (21.028, 105.854) | `261069` | 2312.43 ms |

### 3. Routing Query (Contraction Hierarchies)
| Task | Execution Time | Routing Result |
| :--- | :--- | :--- |
| Route 1 (Cold) | 9.49 ms | Distance: 5.40 km |
| Route 2 (Cached) | 8.78 ms | Travel Time: 6.79 minutes |
