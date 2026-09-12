#include "geocoding_db.h"
#include "address_parser.h"

#include <cmath>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>

// sqlite3.h từ vcpkg
#include <sqlite3.h>


//file này sử dụng trong 2 stage 
//stage 1 : khi chuẩn bị dữ liệu : quét tên đường & poi -> insert vào table trong dtb, quét đoạn đường đánh số nhà
//stage 2 : khi phục vụ query : tìm kiếm POI & trả về lat lon & nội suy tuyến tính dựa trên đầu vào 
void GeocodingDB::execSQL(const char* sql) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string err = errmsg ? errmsg : "Unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error(std::string("SQLite error: ") + err + "\nSQL: " + sql);
    }
}

GeocodingDB::GeocodingDB(const std::string& db_path) {
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc != SQLITE_OK)
        throw std::runtime_error("Cannot open database: " + db_path);

    // Performance tuning (offline preprocessing — WAL không cần)
    execSQL("PRAGMA journal_mode = OFF;");
    execSQL("PRAGMA synchronous  = OFF;");
    execSQL("PRAGMA cache_size   = -64000;");  // 64MB page cache
    execSQL("PRAGMA temp_store   = MEMORY;");

    // -------------------------------------------------------
    // Main POI table — thêm name_norm
    // -------------------------------------------------------
    execSQL(R"(
        CREATE TABLE IF NOT EXISTS places (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            osm_id    INTEGER,
            name      TEXT,
            name_vi   TEXT,
            name_norm TEXT,
            type      TEXT,
            lat       REAL,
            lon       REAL
        );
    )");

    // FTS5: đánh index trên name_norm (bỏ dấu) để tìm không dấu
    // và giữ name gốc để hiển thị
    execSQL(R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS places_fts USING fts5(
            name,
            name_norm,
            type,
            content='places',
            content_rowid='id',
            tokenize='unicode61'
        );
    )");

    // -------------------------------------------------------
    // Address interpolation ranges table
    // -------------------------------------------------------
    execSQL(R"(
        CREATE TABLE IF NOT EXISTS addr_ranges (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            street      TEXT,
            street_norm TEXT,
            num_start   INTEGER,
            num_end     INTEGER,
            lat_start   REAL,
            lon_start   REAL,
            lat_end     REAL,
            lon_end     REAL,
            interp_type TEXT
        );
    )");
    execSQL("CREATE INDEX IF NOT EXISTS idx_addr_street ON addr_ranges(street_norm);");

    // Prepare statements
    rc = sqlite3_prepare_v2(db,
        "INSERT INTO places (osm_id, name, name_vi, name_norm, type, lat, lon)"
        " VALUES (?, ?, ?, ?, ?, ?, ?);",
        -1, &insert_stmt, nullptr);
    if (rc != SQLITE_OK)
        throw std::runtime_error(std::string("Failed to prepare INSERT (places) statement: ") + sqlite3_errmsg(db));

    rc = sqlite3_prepare_v2(db,
        "INSERT INTO addr_ranges"
        " (street, street_norm, num_start, num_end,"
        "  lat_start, lon_start, lat_end, lon_end, interp_type)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);",
        -1, &range_stmt, nullptr);
    if (rc != SQLITE_OK)
        throw std::runtime_error(std::string("Failed to prepare INSERT (addr_ranges) statement: ") + sqlite3_errmsg(db));
}

GeocodingDB::~GeocodingDB() {
    if (insert_stmt) sqlite3_finalize(insert_stmt);
    if (range_stmt)  sqlite3_finalize(range_stmt);
    if (db)          sqlite3_close(db);
}

void GeocodingDB::beginTransaction()  { execSQL("BEGIN TRANSACTION;"); }
void GeocodingDB::commitTransaction() { execSQL("COMMIT;"); }

// ============================================================
// insertPOI — name_norm được tính 1 lần offline
// ============================================================
void GeocodingDB::insertPOI(const POIData& poi) {
    if (poi.name.empty() && poi.name_vi.empty()) return;

    sqlite3_reset(insert_stmt);
    sqlite3_bind_int64 (insert_stmt, 1, poi.osm_id);
    sqlite3_bind_text  (insert_stmt, 2, poi.name.c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (insert_stmt, 3, poi.name_vi.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (insert_stmt, 4, poi.name_norm.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (insert_stmt, 5, poi.type.c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(insert_stmt, 6, poi.lat);
    sqlite3_bind_double(insert_stmt, 7, poi.lon);

    if (sqlite3_step(insert_stmt) != SQLITE_DONE)
        std::cerr << "  Warning: Failed to insert POI " << poi.osm_id << "\n";

    inserted_count++;
    if (inserted_count % 100000 == 0)
        std::cout << "  Inserted " << inserted_count << " POIs...\n";
}

// insertAddrRange
//quét các đoạn đường có số nhà 
void GeocodingDB::insertAddrRange(const AddrRange& range) {
    if (range.street.empty() || range.num_start >= range.num_end) return;

    sqlite3_reset(range_stmt);
    sqlite3_bind_text  (range_stmt, 1, range.street.c_str(),      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text  (range_stmt, 2, range.street_norm.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int   (range_stmt, 3, range.num_start);
    sqlite3_bind_int   (range_stmt, 4, range.num_end);
    sqlite3_bind_double(range_stmt, 5, range.lat_start);
    sqlite3_bind_double(range_stmt, 6, range.lon_start);
    sqlite3_bind_double(range_stmt, 7, range.lat_end);
    sqlite3_bind_double(range_stmt, 8, range.lon_end);
    sqlite3_bind_text  (range_stmt, 9, range.interp_type.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(range_stmt) != SQLITE_DONE)
        std::cerr << "  Warning: Failed to insert addr_range for " << range.street << "\n";
}

// finalizeFTS — xây index sau khi insert xong để sử dụng full text search

void GeocodingDB::finalizeFTS() {
    std::cout << "  Building FTS5 index on places...\n";
    execSQL("INSERT INTO places_fts(places_fts) VALUES('rebuild');");

    execSQL("CREATE INDEX IF NOT EXISTS idx_places_lat ON places(lat);");
    execSQL("CREATE INDEX IF NOT EXISTS idx_places_lon ON places(lon);");
    std::cout << "  FTS5 index done.\n";
}

// searchPOI
// Query FTS5 trên name_norm (không dấu), rank = bm25 × dist_weight
// sử dụng full text search để tìm kiếm POI & trả về lat lon & nội suy tuyến tính dựa trên đầu vào 

std::vector<SearchResult> GeocodingDB::searchPOI(
    const std::string& query_norm,
    std::optional<double> user_lat,
    std::optional<double> user_lon,
    int limit)
{
    std::vector<SearchResult> results;
    if (query_norm.empty()) return results;

    // Xây FTS5 query: "token1* token2* ..."
    std::istringstream iss(query_norm);
    std::string tok, fts_query;
    while (iss >> tok) {
        if (!fts_query.empty()) fts_query += " ";
        fts_query += tok + "*";
    }

    std::string sql;
    bool use_distance = user_lat.has_value() && user_lon.has_value();

    if (use_distance) {
        // Rank kết hợp: text relevance / (1 + khoảng cách degree²)
        sql = R"(
            SELECT p.name, p.type, p.lat, p.lon,
                   (-bm25(places_fts)) /
                   (1.0 + (p.lat - ?) * (p.lat - ?) + (p.lon - ?) * (p.lon - ?))
                   AS score
            FROM places_fts
            JOIN places p ON places_fts.rowid = p.id
            WHERE places_fts MATCH ?
            ORDER BY score DESC
            LIMIT ?;
        )";
    } else {
        // Chỉ dùng FTS5 bm25 khi không có vị trí user
        sql = R"(
            SELECT p.name, p.type, p.lat, p.lon,
                   (-bm25(places_fts)) AS score
            FROM places_fts
            JOIN places p ON places_fts.rowid = p.id
            WHERE places_fts MATCH ?
            ORDER BY score DESC
            LIMIT ?;
        )";
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "searchPOI prepare error: " << sqlite3_errmsg(db) << "\n";
        return results;
    }

    int bind_idx = 1;
    if (use_distance) {
        double ulat = *user_lat, ulon = *user_lon;
        sqlite3_bind_double(stmt, bind_idx++, ulat);
        sqlite3_bind_double(stmt, bind_idx++, ulat);
        sqlite3_bind_double(stmt, bind_idx++, ulon);
        sqlite3_bind_double(stmt, bind_idx++, ulon);
    }
    sqlite3_bind_text(stmt, bind_idx++, fts_query.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, bind_idx++, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SearchResult r;
        auto col_text = [&](int col) -> std::string {
            const char* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return t ? t : "";
        };
        r.name  = col_text(0);
        r.type  = col_text(1);
        r.lat   = sqlite3_column_double(stmt, 2);
        r.lon   = sqlite3_column_double(stmt, 3);
        r.score = sqlite3_column_double(stmt, 4);
        results.push_back(r);
    }
    sqlite3_finalize(stmt);
    return results;
}

// interpolateAddress — nội suy tuyến tính số nhà

std::pair<double,double> GeocodingDB::interpolateAddress(
    const std::string& street_norm,
    int house_number,
    const std::string& interp_type)
{
    // Xây SQL: tìm range phủ house_number trên street
    std::string sql =
        "SELECT num_start, num_end, lat_start, lon_start, lat_end, lon_end, interp_type"
        " FROM addr_ranges"
        " WHERE street_norm = ?"
        "   AND num_start <= ? AND num_end >= ?";

    // Filter theo even/odd nếu cần
    if (interp_type == "even" || interp_type == "odd")
        sql += " AND interp_type = ?";

    sql += " ORDER BY ABS(num_start + num_end - ? * 2)"  // ưu tiên range khít nhất
           " LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
        return {0.0, 0.0};

    int bi = 1;
    sqlite3_bind_text(stmt, bi++, street_norm.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, bi++, house_number);
    sqlite3_bind_int (stmt, bi++, house_number);
    if (interp_type == "even" || interp_type == "odd")
        sqlite3_bind_text(stmt, bi++, interp_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, bi++, house_number);

    std::pair<double,double> result = {0.0, 0.0};

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        int    ns  = sqlite3_column_int   (stmt, 0);
        int    ne  = sqlite3_column_int   (stmt, 1);
        double ls  = sqlite3_column_double(stmt, 2);
        double lo_s= sqlite3_column_double(stmt, 3);
        double le  = sqlite3_column_double(stmt, 4);
        double lo_e= sqlite3_column_double(stmt, 5);
        // std::string itype = ...  (cột 6, bỏ qua)

        if (ne != ns) {
            double t   = static_cast<double>(house_number - ns) / (ne - ns);
            result.first  = ls   + t * (le   - ls);    // lat
            result.second = lo_s + t * (lo_e - lo_s);  // lon
        } else {
            result = {ls, lo_s};
        }
    }

    sqlite3_finalize(stmt);
    return result;
}
