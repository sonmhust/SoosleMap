# C++ Backend Benchmark Results (Hanoi Dataset)

### 1. Data Loading (Zero-parse / Binary fread)
| Tác vụ | File | Dung lượng RAM | Thời gian thực thi | Đánh giá |
| :--- | :--- | :--- | :--- | :--- |
| **Load STR-Tree** | `snap_tree.bin` | ~362 MB | **209.58 ms** | Cực kỳ nhanh (Zero-parse mmap-like speed) |
| **Load CH Graph** | `hanoi_ch.bin` | ~226 MB | **303.12 ms** | Cực kỳ nhanh (SoA CSR layout) |

### 2. Snap-to-Edge Query (STR-Tree BFS)
*Tìm điểm chiếu vuông góc của tọa độ GPS xuống mạng lưới đường giao thông.*

| Tác vụ | Hành động / Địa điểm | Thời gian xử lý | Ghi chú |
| :--- | :--- | :--- | :--- |
| **Snap lần 1** (Cold) | Điểm 1: *Bệnh viện Bạch Mai* | **2.55 ms** | Balanced STR-Tree, O(log N). Giảm từ 2.3 giây xuống 2.55 ms (Nhanh hơn 1000 lần)! |
| **Snap lần 2** (Warm) | Điểm 2: *Hồ Hoàn Kiếm* | **1.85 ms** | Nhanh hơn nhờ CPU L3 Cache (Hot-path caching). |

### 3. Routing Query (Contraction Hierarchies)
*Tìm đường đi ngắn nhất (Shortest Path) cho Ô-tô (Car mode).*
*Lộ trình: Bệnh viện Bạch Mai -> Hồ Hoàn Kiếm.*

| Tác vụ | Thời gian thực thi | Kết quả tìm đường |
| :--- | :--- | :--- |
| **Route lần 1** (Cold) | **8.17 ms** | Khoảng cách: **2.72 km** |
| **Route lần 2** (Cached) | **8.88 ms** | Thời gian chạy xe: **2.86 phút** |

*(Ghi chú: Thuật toán CH (Contraction Hierarchies) kết hợp với thuật toán cây STR-Tree cân bằng đã biến Engine của chúng ta thành một cỗ máy realtime hoàn hảo. Tất cả các thao tác đều phản hồi trong thời gian dưới 10 mili-giây, đáp ứng tiêu chuẩn của một bản đồ chuyên nghiệp).*
