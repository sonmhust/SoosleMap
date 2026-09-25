#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "geocoding_db.h"
#include "graph.h"
#include "serializer.h"
#include "snap_tree.h"
#include "address_parser.h"
#include "routing.h"   // also declares snapNearestCached

namespace py = pybind11;

//khởi tạo module tên routing_engine 
PYBIND11_MODULE(routing_engine, m) {
    m.doc() = "Routing Engine Python Bindings";
    
    //đăng kí lớp SearchResult của C++ vào module m với tên gọi trong python là SearchResult
    py::class_<SearchResult>(m, "SearchResult")
    //binding các thuộc tính của lớp này vào module m với cài đặt cho phép đọc
        .def_readonly("name", &SearchResult::name)  
        .def_readonly("type", &SearchResult::type)
        .def_readonly("lat", &SearchResult::lat)
        .def_readonly("lon", &SearchResult::lon)
        .def_readonly("score", &SearchResult::score);

    // GeocodingDB
    py::class_<GeocodingDB>(m, "GeocodingDB")
        .def(py::init<const std::string&>())    //truyền vào đường dẫn đến file database.sqlite để khởi tạo đối tượng
        .def("search_poi", &GeocodingDB::searchPOI,   //tìm kiếm POI
             py::arg("query_norm"),
             py::arg("user_lat") = std::nullopt,
             py::arg("user_lon") = std::nullopt,
             py::arg("limit") = 10) //truyền tham số cho hàm searchPOI 
        .def("interpolate_address", &GeocodingDB::interpolateAddress,
             py::arg("street_norm"),
             py::arg("house_number"),
             py::arg("interp_type") = "all");

    // AddressQuery
    py::class_<AddressQuery>(m, "AddressQuery")
        .def_readonly("house_number", &AddressQuery::house_number)
        .def_readonly("house_suffix", &AddressQuery::house_suffix)
        .def_readonly("street", &AddressQuery::street)
        .def_readonly("ward", &AddressQuery::ward)
        .def_readonly("district", &AddressQuery::district)
        .def_readonly("city", &AddressQuery::city)
        .def_readonly("is_coord", &AddressQuery::is_coord)
        .def_readonly("coord_lat", &AddressQuery::coord_lat)
        .def_readonly("coord_lon", &AddressQuery::coord_lon);

    m.def("parse_address_query", &parseAddressQuery, "Parse Vietnamese address");

    // Graph
    py::class_<CHGraphQuery>(m, "CHGraphQuery")
        .def(py::init<>())
        .def("get_lat", [](const CHGraphQuery& g, uint32_t u) { return g.lat.size() > u ? g.lat[u] : -1.0; })
        .def("get_lon", [](const CHGraphQuery& g, uint32_t u) { return g.lon.size() > u ? g.lon[u] : -1.0; });

    m.def("load_graph_query", &loadGraphQuery, "Load SoA v2 graph");
    m.def("load_graph_coordinates", &loadGraphCoordinates, "Load only coordinates of SoA graph");

    // SnapTree
    py::class_<SnapTree>(m, "SnapTree")
        .def(py::init<>()); //khởi tạo ko tham số  ,ko có hàm vì dùng để truyền object để truyền vào hàm khác

    //class chứa kết quả trả về , ko cần hàm khởi tạo
    py::class_<SnapResult>(m, "SnapResult")
        .def_readonly("valid", &SnapResult::valid)
        .def_readonly("edge_u", &SnapResult::edge_u)
        .def_readonly("edge_v", &SnapResult::edge_v)
        .def_readonly("t", &SnapResult::t)
        .def_readonly("proj_lat", &SnapResult::proj_lat)
        .def_readonly("proj_lon", &SnapResult::proj_lon)
        .def_readonly("dist_m", &SnapResult::dist_m);

    m.def("load_snap_tree", &loadSnapTree, "Load STR-Tree from binary file");

    m.def("snap_nearest", [](const SnapTree& tree, const CHGraph& graph, double lat, double lon) {
        return snapNearest(tree, graph, lat, lon);
    }, "Snap GPS point to nearest CHGraph edge");

    m.def("snap_nearest", [](const SnapTree& tree, const CHGraphQuery& graph, double lat, double lon) {
        return snapNearestQuery(tree, graph, lat, lon);
    }, "Snap GPS point to nearest CHGraphQuery edge");

    // Cached version: LRU 64 entry, key = GPS rounded to ~1.1m.
    // Cache hit skips STR-Tree BFS (~2ms). Use this instead of snap_nearest in production.
    m.def("snap_nearest_cached", [](const SnapTree& tree, const CHGraphQuery& graph, double lat, double lon) {
        return snapNearestCached(tree, graph, lat, lon);
    }, "Snap GPS point to nearest edge, with LRU cache (64 entries, ~1.1m key precision)");

    // Đăng ký class RouteResult để lưu kết quả tìm đường
    py::class_<RouteResult>(m, "RouteResult")
        .def_readonly("valid", &RouteResult::valid)           // cờ kiểm tra tìm thấy đường hay không
        .def_readonly("distance_m", &RouteResult::distance_m) // tổng khoảng cách đường đi (mét)
        .def_readonly("time_s", &RouteResult::time_s)         // tổng thời gian đi (giây)
        .def_readonly("path_coords", &RouteResult::path_coords) // danh sách tọa độ các điểm trên đường đi
        .def_readonly("path_nodes", &RouteResult::path_nodes);  // danh sách node ID trên đường đi

    // Đăng ký hàm find_shortest_path_ch để gọi thuật toán Bi-directional Dijkstra trên đồ thị CH
    m.def("find_shortest_path_ch", &findShortestPathCH, "Find shortest path on CH graph",
          py::arg("graph"),
          py::arg("source"),
          py::arg("target"),
          py::arg("transport_mode") = 0x01); // Mặc định MODE_CAR = 0x01
}

// Cấu trúc sử dụng trong Python:
// import routing_engine
// # Khởi tạo GeocodingDB với đường dẫn tới file dữ liệu


