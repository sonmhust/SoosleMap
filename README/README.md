# Routing Engine — Hà Nội

Hệ thống tìm đường và tìm kiếm địa chỉ xây dựng từ dữ liệu OpenStreetMap.
Hai chức năng chính: geocoding (text → tọa độ) và routing (tìm đường ngắn nhất).

Dữ liệu đầu vào: hanoi.osm.pbf  
Ngôn ngữ: C++17, pybind11 cho Python binding


---


## Cấu trúc thư mục

```
Google Maps/
├── CMakeLists.txt
├── build/
│   ├── RoutingEngine_Parser.exe      CLI preprocessing
│   ├── routing_engine.*.pyd          Python module
│   ├── sqlite3.dll
│   └── z.dll
├── data/
│   ├── database.sqlite               Geocoding DB (SQLite FTS5)
│   ├── hanoi_ch.bin                  CH graph (SoA CSR, ~296 MB)
│   └── snap_tree.bin                 STR-Tree snap-to-edge (~388 MB)
├── include/
│   ├── graph.h                       Struct CHGraph, CHGraphQuery, HotEdge, ColdEdge
│   ├── osm_parser.h
│   ├── geocoding_db.h
│   ├── contraction.h
│   ├── serializer.h
│   ├── snap_tree.h
│   ├── routing.h
│   ├── haversine.h
│   └── address_parser.h
└── src/
    ├── preprocessing/
    │   ├── main.cpp                  Entry point CLI (pipeline 6 bước)
    │   ├── osm_parser.cpp            2-pass OSM parsing
    │   └── contraction.cpp           CH contraction + witness search
    ├── python/
    │   └── python_bindings.cpp       pybind11 bindings
    ├── geocoding_db.cpp              SQLite FTS5 insert + search
    ├── graph.cpp                     buildGraph()
    ├── routing.cpp                   Bi-directional Dijkstra + unpack shortcut
    ├── serializer.cpp                Binary I/O (SoA CSR)
    └── snap_tree.cpp                 STR-Tree build + snapNearest()
```


---


## Pipeline offline

Lệnh: `RoutingEngine_Parser.exe hanoi.osm.pbf`
File Parser này có tác dụng tiền xử lý, chứa chỉ dẫn ghi dữ liệu ra file bin, với lần lượt các bước dưới đây : 

### Bước 1 — Khởi tạo Geocoding DB

Tạo file data/database.sqlite với schema:

- places(id, osm_id, name, name_vi, name_norm, type, lat, lon)
- places_fts — virtual table FTS5, đánh index trên name_norm (đã bỏ dấu) để tìm không dấu
- streets — lưu đoạn đường phục vụ nội suy số nhà

SQLite pragma: journal_mode=OFF, synchronous=OFF, cache_size=64MB (tối ưu cho ghi batch).


### Bước 2 — Parse OSM

Pass 1 — Quét Way:
- Lọc các highway hợp lệ (motorway → track, footway, cycleway, ...)
- Xác định chiều đường (oneway hay 2 chiều)
- Ghi nhận tập valid_nodes: các node ID xuất hiện trong đường (có nhiều nodes xuất hiện trong tập node nhưng k nằm trên đường -> ko valid)
- Insert tên đường vào SQLite, gom nhiều lệnh insert thành 1 batch

Pass 2 — Quét Node:
- Lấy tọa độ (lat, lon) của các node thuộc valid_nodes
- Insert POI/amenity vào SQLite kèm lat/lon tương ứng

### Bước 3 — Build đồ thị CHGraph cho dữ liệu thô
CHEdge : 1 cạnh trong đồ thị đã được co rút bằng CH
Struct CHEdge: { target, weight, mid_node, transport_mode, next }
    - target : đỉnh đích
    - weight : trọng số tính bằng giây (thời gian di chuyển từ u->v) : Haversine(u, v) / speed_kmh
    - mid_node : list các node ở giữa 2 đỉnh u->v khi bị co rút bởi CH
    - transport_mode : loại phương tiện được phép di chuyển trên cạnh này
    - next : index cạnh tiếp theo nối với cùng 1 đỉnh trong edge pool 

Toàn bộ bản đồ trước khi CH được lưu bằng CTDL CHGraph dạng Structure of Array, CHGraph chứa các array gồm : array cho lat , lon, rank, bool CH status của điểm thứ i, cạnh bắt đầu duyệt của đỉnh thứ i (duyệt theo dạng linked-list trong Edgepool theo con trỏ next trong CHEdge)


Transport mode bitmask:
- CAR        = 0x01
- MOTORBIKE  = 0x02
- BICYCLE    = 0x04
- PEDESTRIAN = 0x08

Bảng tốc độ theo highway type:
- motorway=100, trunk=80, primary=60, secondary=50, tertiary=40
- residential=30, service=20, footway=5, cycleway=15, steps=3 (km/h)


### Bước 4 — Contraction Hierarchies

1. Tính Priority heuristic cho mỗi đỉnh (ranking)
2. Co rút đỉnh từ rank thấp lên cao:
   - Chạy Witness Search (Dijkstra cục bộ) để kiểm tra xem có đường tắt qua đỉnh khác không
   - Nếu không có nhân chứng: sinh Shortcut nối 2 hàng xóm, lưu mid_node để unpack sau
3. Chạy với vehicle_mode = CAR | MOTORBIKE để xây cây CH cho loại xe này


### Bước 5 — Serialize ra hanoi_ch.bin
Tạo ra file đồ thị để load vào bộ nhớ,
CTDL Structure of Array & Compressed Sparse Row thay cho linked_list
Format SoA CSR (Structure-of-Arrays + Compressed Sparse Row):

File bin gồm : 
1.FileHeader   20B    magic=0x43484752("CHGR") (Chữ ký xác định file CHGraph), num_nodes (số đỉnh), num_fwd(số cạnh chiều đi), num_bwd(số cạnh chiều về) (3x4B = 12B)
    
2. SerNodeHot : mảng N+1 phần tử, mỗi phần tử 12B, chứa rank/fwd_offset/bwd_offset
    Phần tử [N] là sentinel: {rank=-1, fwd_offset=total_fwd, bwd_offset=total_bwd}
    Dijkstra dùng offset[u] và offset[u+1] → không cần if-check cho đỉnh cuối
    SerNodeHot    (N+1) × 12B   rank(4)/fwd_offset(4)/bwd_offset(4)
    Mật độ cache: 5.3 đỉnh/cache-line (so với 2.3 khi dùng SerNode 28B cũ)
    
3. SerNodeCold : mảng N phần tử (không sentinel), chứa lat/lon
    Chỉ đọc khi snap GPS xuống cạnh hoặc unpack shortcut path
    SerNodeCold   N × 16B       lat(8)/lon(8)
    Mật độ cache: 4 đỉnh/cache-line (perfectly packed)

4. FwdHot/BwdHot : mảng lưu các cạnh theo CSR, mỗi phần tử 12B
    Định u có 5 cạnh trong FwdHot → 5 cạnh xếp sát nhau từ ofs → ofs+4
    
5. FwdCold/BwdCold : lưu đỉnh giữa của các cạnh shortcut

    FwdHot       num_fwd × 12B  target(4)/weight(4)/transport_mode(1)/pad(3)
    FwdCold      num_fwd ×  4B  mid_node (chỉ đọc khi unpack)
    BwdHot       num_bwd × 12B
    BwdCold      num_bwd ×  4B

Tách Hot/Cold: Dijkstra chỉ đọc SerNodeHot + FwdHot/BwdHot, giảm cache miss ~57%.

### Bước 6 — Build STR-Tree ra snap_tree.bin

Mục đích: khi user click bản đồ, tọa độ GPS thường không nằm trên tim đường.
STR-Tree tìm cạnh đường gần nhất và chiếu điểm GPS xuống cạnh đó trong O(log N).

STR-Tree chỉ index cạnh gốc, bỏ shortcut:
- Shortcut là cạnh ảo không có tọa độ thực trên bản đồ
- Lọc cạnh gốc : các cạnh có mid_node = NO_SHORTCUT 

Struct STRNode (48 bytes):1 node trong cây, dùng chung cho cả leaf và internal node
```
double min_lat, min_lon, max_lat, max_lon   Tọa độ BBox (32B)
uint32_t child_start            với internal: index con đầu; leaf: unused
uint32_t child_end              với internal: index con cuối+1, leaf: bit31=LEAF_FLAG
uint32_t edge_u, edge_v         với internal : unused , leaf only: 2 đầu cạnh
```
- Internal node: BBox bao nhóm con
- Leaf node: BBox của đoạn u→v + lưu edge_u, edge_v
- nodes[0] = root

Build (Sort-Tile-Recursive):
1. Với mỗi cạnh gốc (u→v): tính BBox + centroid
2. buildRecursive() đệ quy: sort lat → chia strip → sort lon → nhóm leaf_cap=16
    Ban đầu tất cả lá nằm ở 1 mảng phẳng, sau đó sort theo X, rồi chia 250 nhóm, mỗi nhóm sort theo Y, vẫn trong mảng phẳng đó
    Sort theo X xong, ta cắt bản đồ thành 250 dải dọc (Slices). Nhờ nhốt vào 250 dải này, độ rộng ngang (X) của chúng đã bị giới hạn lại rất hẹp.
    Bên trong mỗi dải hẹp đó, ta lại Sort theo Y (Vĩ độ), rồi mới bốc 16 phần tử nhét vào BBox.
    Vì 16 phần tử này bị kẹt trong một dải hẹp (X xấp xỉ nhau), nay lại được Sort theo Y (Y cũng xấp xỉ nhau) => BBox sinh ra sẽ có hình dáng VUÔNG VỨC và túm tụm lại tại 1 khu vực nhỏ xíu
    
3. Reverse flat array để root về index 0 


Binary format (snap_tree.bin):
```
"SNAP"(4B) (chữ kí file) | version:u32 | num_graph_nodes:u32 | num_graph_edges:u32 | num_str_nodes:u64 (số node) | STRNode[] (mảng các STRNode dưới dạng flat array)
```
Cây được build theo dạng bottom-up (gom từng đường vào bbox trước rồi mới cho vào bbox lớn hơn), với mỗi internal node có tối đa 16 node con -> tổng có 5 tầng, tối ưu cache. 
Load bằng fread 1 lần, zero-parse.

snapNearest(lat, lon) — Best-First Search:
1. Push root vào min-heap (key = minDistSq tới BBox)
2. Pop node gần nhất ra khỏi heap để sử dụng, nếu như node được pop là:
   - Internal: push tất cả children vào heap, auto arrange theo khoảng cách tới BBox
   - Leaf: tính điểm chiếu P trên đoạn (u→v), đo Haversine(Q, P)
3. Pruning: dist_to_bbox² (khoảng cách từ Q tới viền BBox đang xét (dùng euclide cho internal node)) >= best_dist² (khoảng cách từ Q tới Best snap candidate tìm được từ trước) thì skip toàn bộ subtree (cắt tỉa sớm)
    Nếu là leaf node, thay vì dùng haversine -> dùng euclide + lon_scale để bóp méo -> tính heuristic, nếu heuristic > best_dist^2 -> cắt tỉa, nếu bé hơn thì mới dùng haversine để tính chính xác

Công thức chiếu vuông góc:
```
t = clamp( dot(Q-U, V-U) / |V-U|², 0, 1 )
P = U + t × (V - U)
dist = Haversine(Q, P)  (mét)
```
---

## Online Query (Python)

### Khởi động

```python
import routing_engine

graph = routing_engine.CHGraphQuery()
routing_engine.load_graph_query(graph, "data/hanoi_ch.bin")

snap = routing_engine.SnapTree()
routing_engine.load_snap_tree(snap, "data/snap_tree.bin")

db = routing_engine.GeocodingDB("data/database.sqlite")
```


### Snap tọa độ GPS xuống cạnh đường

```python
result = routing_engine.snap_nearest(snap, graph, lat, lon)

# result.valid           True nếu tìm thấy cạnh
# result.edge_u/edge_v   2 node đầu của cạnh gần nhất (index trong graph)
# result.t               tham số nội suy [0,1]: P = U + t*(V-U)
# result.proj_lat/lon    tọa độ điểm chiếu P trên cạnh
# result.dist_m          Haversine(GPS, P) tính bằng mét
```

Lưu ý: proj_lat/proj_lon là điểm nằm giữa cạnh, không phải node nào trong graph.
Dijkstra chỉ xuất phát từ node, nên cần chọn edge_u hoặc edge_v gần điểm chiếu hơn:

```python
def pick_node(res, graph):
    u, v = res.edge_u, res.edge_v
    du = (graph.get_lat(u) - res.proj_lat)**2 + (graph.get_lon(u) - res.proj_lon)**2
    dv = (graph.get_lat(v) - res.proj_lat)**2 + (graph.get_lon(v) - res.proj_lon)**2
    return u if du <= dv else v

start_node = pick_node(snap_start, graph)
end_node   = pick_node(snap_end,   graph)
```


### Tìm đường

```python
route = routing_engine.find_shortest_path_ch(
    graph,
    source=start_node,
    target=end_node,
    transport_mode=0x01   # MODE_CAR
)

# route.valid         True nếu tìm thấy đường
# route.distance_m    tổng khoảng cách (mét)
# route.time_s        tổng thời gian (giây)
# route.path_coords   list[(lat, lon)] các điểm trên đường
# route.path_nodes    list[node_id]
```

Thuật toán Bi-directional Dijkstra trên CH:
- Forward từ source: chỉ relax cạnh tới node có rank[v] > rank[u]
- Backward từ target: tương tự
- Dừng khi dist_f_top + dist_b_top >= best_weight
- Unpack shortcut đệ quy qua mid_node để lấy tọa độ thực tế


### Geocoding

```python
# Parse địa chỉ tiếng Việt
addr = routing_engine.parse_address_query("15 Lý Thường Kiệt, Hoàn Kiếm")
# addr.house_number, addr.street, addr.ward, addr.district, addr.city

# Tìm kiếm POI
results = db.search_poi(query_norm="ly thuong kiet", user_lat=21.02, user_lon=105.84, limit=10)
# results[i].name, .type, .lat, .lon, .score

# Nội suy số nhà
coords = db.interpolate_address(street_norm="ly thuong kiet", house_number="15")
```


---


## Tech stack

| Thành phần       | Công nghệ                        |
|-----------------|----------------------------------|
| Parser OSM      | libosmium 2.23.1                 |
| Geocoding DB    | SQLite3 + FTS5                   |
| Spatial index   | STR-Tree tự cài (flat array)     |
| Routing         | Bi-directional Dijkstra trên CH  |
| Binary I/O      | fread/fwrite, SoA CSR format     |
| Python binding  | pybind11                         |
| Build           | CMake, MSVC                      |


---


## Trạng thái

- [x] OSM Parser (2-pass, libosmium)
- [x] Geocoding DB (SQLite FTS5 + address interpolation)
- [x] Graph build + Contraction Hierarchies
- [x] Serializer (SoA CSR binary)
- [x] STR-Tree snap-to-edge
- [x] Bi-directional Dijkstra + shortcut unpack
- [x] Python bindings (pybind11)
- [ ] REST API / Frontend