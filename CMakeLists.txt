#cmake có tác dụng gom các file .cpp riêng lẻ để tự động hướng dẫn compiler build ra file thực thi tùy hệ điều hành
#với CMakeList.txt :
#(nếu tạo file thực thi -> 1 trong các file phải là file main hoặc có chứa hàm main)
#Nếu tạo thư viện -> cmake gom file tạo thành 1 thư viện động, cung cấp hàm
cmake_minimum_required(VERSION 3.10)
project(RoutingEngine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 1. Find ZLIB
find_package(ZLIB REQUIRED)

# 3. Find SQLite3 (required for geocoding database)
# Trên Linux/Docker: libsqlite3-dev cung cấp SQLite::SQLite3 chuẩn
# Trên Windows (vcpkg): cài unofficial-sqlite3 thủ công nếu cần
find_package(SQLite3 REQUIRED)
set(SQLITE3_LIB SQLite::SQLite3)

# 4. Find Pybind11 via FetchContent
include(FetchContent)
FetchContent_Declare(
  pybind11
  URL https://github.com/pybind/pybind11/archive/refs/tags/v2.13.5.zip
  TLS_VERIFY OFF
)
FetchContent_MakeAvailable(pybind11)





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
    ${SQLITE3_LIB}
)
