# Lộ Trình Triển Khai Hệ Thống Phân Tán (Deployment Roadmap)

Tài liệu này mô tả kiến trúc và quy trình triển khai dự án Routing Engine (Core C++) thành một hệ thống Web Service độc lập, có khả năng mở rộng từ quy mô cá nhân đến hàng triệu người dùng.

---

## Kiến Trúc Tổng Thể (High-Level Architecture)

Hệ thống phân thành 2 vùng rõ rệt: tầng hạ tầng mạng toàn cầu (nằm ngoài máy ảo) và tầng ứng dụng chạy bên trong máy ảo.

```
Internet
    │
    ▼
[Cloudflare CDN / WAF]          # Hạ tầng ngoài: chặn DDoS, cache JSON API
    │
    ▼
[Cloud Load Balancer]           # Hạ tầng ngoài: Oracle LB hoặc AWS ALB (miễn phí trên Oracle)
    │
    └──────────────── Oracle VM (ARM 4vCPU / 24GB RAM) ─────────────────┐
                           │                                             │
                      [Container: Nginx]        ← Cache tầng mạng       │
                           │                                             │
                      [Container: FastAPI]       ← Python + C++ (.so)   │
                           │                       (Pybind11, nhúng     │
                      [Container: Redis]            trực tiếp vào RAM,  │
                                                    0ms delay)          │
                           │                                             │
                      [Volume: /app/data]        ← Ổ cứng chia sẻ      │
                       └─ hanoi_ch.bin (RAM)                            │
                       └─ snap_tree.bin (RAM)                           │
                       └─ database.sqlite (Đĩa, FTS5 truy vấn trực tiếp)
                                                                        │
    └───────────────────────────────────────────────────────────────────┘
```

**Khi Scale Up (Kubernetes trên Oracle OKE):**
Kubernetes gộp nhiều VM lại. Container FastAPI được nhân bản hàng trăm bản, trong khi Nginx (3 bản) và Redis (3 bản tạo thành Cluster tập trung) vẫn giữ số lượng tối thiểu. Dữ liệu bản đồ sử dụng NFS (Oracle FSS) để chia sẻ giữa 100 VM mà không cần tải về từng máy.

---

## 1. Hạ Tầng Mạng Toàn Cầu (Bên Ngoài Máy Ảo)

Hai lớp đứng ngoài Internet, thuộc cơ sở hạ tầng được quản lý bởi nhà cung cấp đám mây:

- **Cloudflare (CDN / WAF):** Cache cấp toàn cầu cho các phản hồi JSON trùng lặp và chống tấn công từ chối dịch vụ (DDoS).
- **Cloud Load Balancer:** Phân phối Traffic đến các máy ảo. Oracle cung cấp 1 Load Balancer linh hoạt miễn phí trọn đời trên Always Free Tier.

---

## 2. Hạ Tầng Máy Ảo (Bên Trong VM)

Toàn bộ ứng dụng chạy trong Docker Compose gồm 3 Container độc lập giao tiếp với nhau qua mạng nội bộ Docker:

### Container 1: Nginx (Trạm Gác và Cache Tầng Mạng)
- Mở Port 80/443 tiếp nhận traffic từ Internet.
- Bộ đệm Proxy Cache: nếu URL trùng khớp, trả về JSON ngay lập tức (< 1ms) mà không động đến FastAPI.
- Xử lý nén GZIP, mã hóa HTTPS, Rate Limiting.

### Container 2: FastAPI + C++ Engine (Lõi Ứng Dụng)
- Chứa mã nguồn Python (FastAPI) và file nhị phân C++ (.so).
- Module C++ (Tìm kiếm & Định tuyến) được nhúng trực tiếp vào tiến trình Python thông qua Pybind11. Giao tiếp nội bộ giữa Python và C++ sử dụng RAM chung, không qua cổng mạng nào (Zero-latency).
- Chạy đa tiến trình (Gunicorn + Uvicorn Workers) để tận dụng toàn bộ CPU.
- Sự kiện khởi động (Startup Hook) nạp `hanoi_ch.bin` và `snap_tree.bin` vào RAM.

### Container 3: Redis (Cache Phân Tán Tầng Ứng Dụng)
- Một Container Redis duy nhất trên VM, dùng chung bởi tất cả các Worker của FastAPI.
- Lưu trữ kết quả Geocoding (tên POI → toạ độ) với TTL 24h để tránh tra cứu lặp lại SQLite.
- Sử dụng Local RAM khi chỉ có 1 VM; Redis là bắt buộc khi mở rộng lên nhiều VM do đảm bảo tính nhất quán dữ liệu giữa các Container.

---

## 3. Quản Lý Dữ Liệu

### Phân Tách Qua Volume
Dữ liệu bản đồ không được đóng gói vào Docker Image để tránh phình kích thước. Thư mục `./data` trên máy ảo được ánh xạ vào `/app/data` bên trong Container.

### Khởi Tạo Tự Động (init-data.sh)
Kịch bản `init-data.sh` chạy trước khi FastAPI khởi động. Sử dụng `wget`/`curl` (đa nền tảng) để tải file bản đồ nén từ S3/GCS qua Pre-signed URL, sau đó giải nén vào Volume.

### SQLite và Fuzzy Search (FTS5)
Cơ sở dữ liệu tìm kiếm địa điểm dùng SQLite với module FTS5 và thuật toán xếp hạng BM25. Thư viện `libsqlite3-dev` trên Linux Docker tích hợp FTS5 sẵn, cho phép truy vấn Fuzzy Search trực tiếp từ ổ cứng mà không tốn RAM.

### Mở Rộng Lưu Trữ (NFS - Khi Scale lên K8s)
Khi nhân bản lên hàng trăm máy ảo qua Kubernetes, sử dụng ổ đĩa mạng chia sẻ (Oracle FSS / AWS EFS). Chỉ 1 bản sao dữ liệu được lưu trữ trung tâm, tất cả VM kết nối (Mount) vào ổ đĩa này. SQLite cho phép hàng ngàn luồng đọc đồng thời (Read-only) mà không xảy ra Data Race.

---

## 4. Tối Ưu Hóa Hiệu Năng (Multi-Tier Caching)

| Tầng | Công Nghệ | Thời Gian Phản Hồi | Cơ Chế |
|---|---|---|---|
| Tầng Mạng (Proxy) | Nginx / Cloudflare | < 1ms | Cache toàn bộ JSON phản hồi theo URL |
| Tầng Ứng Dụng | Redis | 2 - 5ms | Cache kết quả Geocoding và chặng đường |
| Tầng C++ (Core) | RAM nội bộ | 4.5ms | Nạp Graph lên RAM, FTS5 truy vấn trực tiếp |

---

## 5. Phân Tích Hiệu Năng (Từ BenchmarkResultV3)

| Kịch bản | Tổng Độ Trễ Backend | Ghi Chú |
|---|---|---|
| Cache Miss hoàn toàn | 12ms - 22ms | Geocoding SQLite + C++ 4.5ms + FastAPI overhead |
| Cache Hit tầng Redis | 2ms - 5ms | Bỏ qua SQLite, C++ vẫn chạy |
| Cache Hit tầng Nginx/Cloudflare | < 1ms | FastAPI không nhận Request |

---

## 6. Lộ Trình Triển Khai (Các bước thực thi)

1. Cài đặt Docker Desktop trên Windows (môi trường phát triển)
2. Viết `Dockerfile` (Multi-stage Build)
3. Viết `app/main.py` (FastAPI + Geocoding + Redis)
4. Viết `init-data.sh` (Script khởi tạo dữ liệu)
5. Viết `docker-compose.yml` (Nginx + FastAPI + Redis)
6. Sửa `CMakeLists.txt` (Loại bỏ vcpkg, dùng chuẩn Linux)
7. Build và kiểm thử toàn bộ hệ thống (`docker-compose up --build`)
8. Đăng ký Oracle Cloud và cấu hình VM ARM
9. Đẩy Image lên Docker Hub / GitHub Container Registry
10. Triển khai và kiểm thử trên môi trường Oracle VM

---
*Lựa chọn hạ tầng: Oracle Cloud Always Free (4 vCPU ARM / 24GB RAM) + Oracle OKE (Kubernetes) khi Scale Up.*
