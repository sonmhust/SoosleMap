#cmake có tác dụng gom các file .cpp riêng lẻ để tự động hướng dẫn compiler build ra file thực thi tùy hệ điều hành
#với CMakeList.txt :
#(nếu tạo file thực thi -> 1 trong các file phải là file main hoặc có chứa hàm main)
#Nếu tạo thư viện -> cmake gom file tạo thành 1 thư viện động, cung cấp hàm
cmake_minimum_required(VERSION 3.10)
project(RoutingEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Find osmium headers
find_path(OSMIUM_INCLUDE_DIRS "osmium/version.hpp")

# 2. Find ZLIB (required for PBF decompression)
find_package(ZLIB REQUIRED)

# 3. Find SQLite3 (required for geocoding database)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

# 4. Find Pybind11 via FetchContent
include(FetchContent)
FetchContent_Declare(
  pybind11
  URL https://github.com/pybind/pybind11/archive/refs/tags/v2.13.5.zip
  TLS_VERIFY OFF
)
FetchContent_MakeAvailable(pybind11)


# RoutingEngine_Parser
#   - Parses OSM PBF
#   - Runs CH contraction
#   - Serializes directly to SoA CSR (hanoi_ch.bin) — no converter
#   - Builds STR-tree (snap_tree.bin)
#   - Streams POIs to SQLite (database.sqlite)

add_executable(RoutingEngine_Parser
    src/preprocessing/main.cpp
    src/preprocessing/osm_parser.cpp
    src/geocoding_db.cpp
    src/graph.cpp
    src/preprocessing/contraction.cpp
    src/serializer.cpp
    src/snap_tree.cpp
    src/address_parser.cpp
)
target_include_directories(RoutingEngine_Parser PRIVATE include ${OSMIUM_INCLUDE_DIRS})
target_link_libraries(RoutingEngine_Parser PRIVATE
    ZLIB::ZLIB
    unofficial::sqlite3::sqlite3
)


# query_server  — interactive CLI for route queries
add_executable(query_server
    src/query_server.cpp
    src/snap_tree.cpp
    src/serializer.cpp
    src/graph.cpp
    src/routing.cpp
)
target_include_directories(query_server PRIVATE include ${OSMIUM_INCLUDE_DIRS})
target_link_libraries(query_server PRIVATE
    ZLIB::ZLIB
    unofficial::sqlite3::sqlite3
)


# routing_engine — Python extension module (pybind11)

pybind11_add_module(routing_engine
    src/python/python_bindings.cpp
    src/geocoding_db.cpp
    src/graph.cpp
    src/serializer.cpp
    src/snap_tree.cpp
    src/address_parser.cpp
    src/routing.cpp
)
target_include_directories(routing_engine PRIVATE include)
target_link_libraries(routing_engine PRIVATE
    ZLIB::ZLIB
    unofficial::sqlite3::sqlite3
)
