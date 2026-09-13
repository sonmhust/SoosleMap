# C++ Backend Benchmark Results (Hanoi Dataset)

### 1. Data Loading (Zero-parse / Binary fread)
| Tác vụ | File | Dung lượng RAM | Thời gian thực thi | Đánh giá |
| :--- | :--- | :--- | :--- | :--- |
| **Load STR-Tree** | `snap_tree.bin` | ~362 MB | **215.96 ms** | Cực kỳ nhanh (Zero-parse mmap-like speed) |
| **Load CH Graph** | `hanoi_ch.bin` | ~226 MB | **328.10 ms** | Cực kỳ nhanh (SoA CSR layout) |

### 2. Snap-to-Edge Query (STR-Tree BFS)
*Tìm điểm chiếu vuông góc của tọa độ GPS xuống mạng lưới đường giao thông.*

| Tác vụ | Tọa độ GPS | Node gần nhất | Thời gian thực thi |
| :--- | :--- | :--- | :--- |
| **Snap lần 1** (Cold) | Bệnh viện Bạch Mai (21.001, 105.841) | `149292` | **2364.07 ms** |
| **Snap lần 2** (Warm) | Hồ Hoàn Kiếm (21.028, 105.854) | `261069` | **2312.43 ms** |

*(Ghi chú: Tốc độ Snap hiện tại dao động quanh mức ~2.3s do cây STR-Tree bản đồ quá chi tiết hoặc chưa cấu hình Pruning tối ưu nhất, tuy nhiên thời gian caching giữa lần 1 và lần 2 cải thiện khoảng ~50ms nhờ CPU L3 cache).*

### 3. Routing Query (Contraction Hierarchies)
*Tìm đường đi ngắn nhất (Shortest Path) cho Ô-tô (Car mode).*
*Lộ trình: Bệnh viện Bạch Mai -> Hồ Hoàn Kiếm.*

| Tác vụ | Thời gian thực thi | Kết quả tìm đường |
| :--- | :--- | :--- |
| **Route lần 1** (Cold) | **9.49 ms** | Khoảng cách: **5.40 km** |
| **Route lần 2** (Cached) | **8.78 ms** | Thời gian chạy xe: **6.79 phút** |

*(Ghi chú: Thuật toán CH (Contraction Hierarchies) chạy cực kỳ xuất sắc. Nó xuyên thủng qua đồ thị 1.8 triệu đỉnh của Hà Nội chỉ trong chưa tới 10 mili-giây! Lần truy vấn thứ 2 còn nhanh hơn ~0.7ms do đồ thị HotEdge đã nằm trên cache).*
