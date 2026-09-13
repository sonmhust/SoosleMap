#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

// ============================================================
// POI để insert vào database
// ============================================================
struct POIData {
    int64_t     osm_id = 0;
    std::string name;
    std::string name_vi;
    std::string name_norm;  // Chuỗi tên đã loại bỏ dấu và chuyển sang chữ thường (được xử lý ngoại tuyến)
    std::string type;       // "street", "amenity", "shop", etc.
    double      lat = 0.0;
    double      lon = 0.0;
};

// ============================================================
// Address interpolation range (từ addr:interpolation way trong OSM)
// ============================================================
struct AddrRange {
    std::string street;       // tên đường gốc
    std::string street_norm;  // đã bỏ dấu, lowercase
    int         num_start  = 0;
    int         num_end    = 0;
    double      lat_start  = 0.0, lon_start = 0.0;
    double      lat_end    = 0.0, lon_end   = 0.0;
    std::string interp_type;  // "all", "even", "odd"
};

// ============================================================
// Kết quả tìm kiếm địa điểm
// ============================================================
struct SearchResult {
    std::string name;
    std::string type;
    double      lat   = 0.0;
    double      lon   = 0.0;
    double      score = 0.0;  // combined text+distance score (higher = better)
};

// ============================================================
// GeocodingDB
// ============================================================
class GeocodingDB {
private:
    sqlite3*      db          = nullptr;
    sqlite3_stmt* insert_stmt = nullptr;
    sqlite3_stmt* range_stmt  = nullptr;
    int           inserted_count = 0;

    void execSQL(const char* sql);

public:
    GeocodingDB(const std::string& db_path);
    ~GeocodingDB();

    // --- Transaction control ---
    void beginTransaction();
    void commitTransaction();

    // --- Insert ---
    void insertPOI(const POIData& poi);
    void insertAddrRange(const AddrRange& range);

    // --- Xây dựng chỉ mục (được gọi sau khi hoàn tất giao dịch) ---
    void finalizeFTS();

    // --- Query ---
    // Tìm kiếm địa điểm theo text.
    // Tọa độ người dùng: nếu được cung cấp sẽ ưu tiên xếp hạng theo khoảng cách; nếu bỏ trống sẽ sử dụng thuật toán BM25 thuần túy
    std::vector<SearchResult> searchPOI(
        const std::string& query_norm,
        std::optional<double> user_lat = std::nullopt,
        std::optional<double> user_lon = std::nullopt,
        int limit = 10
    );

    // Nội suy địa chỉ số nhà: trả {lat, lon} hoặc {0,0} nếu không tìm thấy
    std::pair<double, double> interpolateAddress(
        const std::string& street_norm,
        int house_number,
        const std::string& interp_type = "all"  // "all", "even", "odd", ""=any
    );

    int getInsertedCount() const { return inserted_count; }
};
