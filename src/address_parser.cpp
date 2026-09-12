#include "address_parser.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

//chuẩn hóa tìm kiếm địa chỉ khi nhập để hoạt động với sqlite + fuzzy matching
//bảng latin hóa tiếng việt

static const std::unordered_map<std::string, std::string> VIET_MAP = {
    // a
    {"à","a"},{"á","a"},{"ả","a"},{"ã","a"},{"ạ","a"},
    {"ă","a"},{"ắ","a"},{"ằ","a"},{"ẳ","a"},{"ẵ","a"},{"ặ","a"},
    {"â","a"},{"ấ","a"},{"ầ","a"},{"ẩ","a"},{"ẫ","a"},{"ậ","a"},
    // e
    {"è","e"},{"é","e"},{"ẻ","e"},{"ẽ","e"},{"ẹ","e"},
    {"ê","e"},{"ế","e"},{"ề","e"},{"ể","e"},{"ễ","e"},{"ệ","e"},
    // i
    {"ì","i"},{"í","i"},{"ỉ","i"},{"ĩ","i"},{"ị","i"},
    // o
    {"ò","o"},{"ó","o"},{"ỏ","o"},{"õ","o"},{"ọ","o"},
    {"ô","o"},{"ố","o"},{"ồ","o"},{"ổ","o"},{"ỗ","o"},{"ộ","o"},
    {"ơ","o"},{"ớ","o"},{"ờ","o"},{"ở","o"},{"ỡ","o"},{"ợ","o"},
    // u
    {"ù","u"},{"ú","u"},{"ủ","u"},{"ũ","u"},{"ụ","u"},
    {"ư","u"},{"ứ","u"},{"ừ","u"},{"ử","u"},{"ữ","u"},{"ự","u"},
    // y
    {"ỳ","y"},{"ý","y"},{"ỷ","y"},{"ỹ","y"},{"ỵ","y"},
    // d
    {"đ","d"},{"Đ","d"},
    // Uppercase
    {"À","a"},{"Á","a"},{"Ả","a"},{"Ã","a"},{"Ạ","a"},
    {"Ă","a"},{"Ắ","a"},{"Ằ","a"},{"Ẳ","a"},{"Ẵ","a"},{"Ặ","a"},
    {"Â","a"},{"Ấ","a"},{"Ầ","a"},{"Ẩ","a"},{"Ẫ","a"},{"Ậ","a"},
    {"È","e"},{"É","e"},{"Ẻ","e"},{"Ẽ","e"},{"Ẹ","e"},
    {"Ê","e"},{"Ế","e"},{"Ề","e"},{"Ể","e"},{"Ễ","e"},{"Ệ","e"},
    {"Ì","i"},{"Í","i"},{"Ỉ","i"},{"Ĩ","i"},{"Ị","i"},
    {"Ò","o"},{"Ó","o"},{"Ỏ","o"},{"Õ","o"},{"Ọ","o"},
    {"Ô","o"},{"Ố","o"},{"Ồ","o"},{"Ổ","o"},{"Ỗ","o"},{"Ộ","o"},
    {"Ơ","o"},{"Ớ","o"},{"Ờ","o"},{"Ở","o"},{"Ỡ","o"},{"Ợ","o"},
    {"Ù","u"},{"Ú","u"},{"Ủ","u"},{"Ũ","u"},{"Ụ","u"},
    {"Ư","u"},{"Ứ","u"},{"Ừ","u"},{"Ử","u"},{"Ữ","u"},{"Ự","u"},
    {"Ỳ","y"},{"Ý","y"},{"Ỷ","y"},{"Ỹ","y"},{"Ỵ","y"},
};

std::string normalizeVietnamese(const std::string& s) {
    std::string result;
    //de danh s.size de ko phai realloc
    result.reserve(s.size());

    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);

        // Xác định độ dài UTF-8 sequence
        int seq_len = 1;
        if      ((c & 0xF0) == 0xF0) seq_len = 4;
        else if ((c & 0xE0) == 0xE0) seq_len = 3;
        else if ((c & 0xC0) == 0xC0) seq_len = 2;

        if (seq_len > 1 && i + seq_len <= s.size()) {
            std::string multi = s.substr(i, seq_len);
            auto it = VIET_MAP.find(multi);
            if (it != VIET_MAP.end()) {
                result += it->second;
            } else {
                // Ký tự multi-byte không có trong bảng: giữ nguyên
                result += multi;
            }
            i += seq_len;
        } else {
            // ASCII: chuyển lowercase
            result += static_cast<char>(std::tolower(c));
            i += 1;
        }
    }

    // Chuẩn hóa khoảng trắng: gộp nhiều space thành 1
    std::string out;
    out.reserve(result.size());
    bool prev_space = true; // true để trim đầu
    for (char ch : result) {
        if (ch == ' ' || ch == '\t') {
            if (!prev_space) { out += ' '; prev_space = true; }
        } else {
            out += ch;
            prev_space = false;
        }
    }
    // Trim cuối
    while (!out.empty() && out.back() == ' ') out.pop_back();

    return out;
}

// ============================================================
// Internal helpers
// ============================================================

static std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Tách chuỗi theo delimiter
static std::vector<std::string> splitBy(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::string token;
    std::istringstream iss(s);
    while (std::getline(iss, token, delim)) {
        std::string t = trim(token);
        if (!t.empty()) parts.push_back(t);
    }
    return parts;
}

// Bảng viết tắt đơn vị hành chính → normalize key
static const std::unordered_map<std::string, std::string> ADMIN_PREFIX = {
    // Phường / Ward
    {"phuong","ward"}, {"p.","ward"}, {"p ","ward"},
    // Xã
    {"xa","ward"},
    // Quận / District
    {"quan","district"}, {"q.","district"}, {"q ","district"},
    // Huyện
    {"huyen","district"}, {"h.","district"},
    // Thị xã
    {"thi xa","district"},
    // Thành phố / Tỉnh
    {"thanh pho","city"}, {"tp.","city"}, {"tp ","city"},
    {"tinh","city"}, {"t.","city"},
    // Thị trấn
    {"thi tran","district"},
};

// Bảng tên thành phố viết tắt thông dụng
static const std::unordered_map<std::string, std::string> CITY_ALIAS = {
    {"hn","ha noi"}, {"ha noi","ha noi"}, {"hanoi","ha noi"},
    {"hcm","ho chi minh"}, {"tphcm","ho chi minh"},
    {"ho chi minh","ho chi minh"}, {"saigon","ho chi minh"},
    {"dn","da nang"}, {"da nang","da nang"},
    {"hp","hai phong"}, {"hai phong","hai phong"},
    {"ct","can tho"}, {"can tho","can tho"},
};

// Bảng prefix loại đường → loại bỏ khi search FTS
static const std::vector<std::string> ROAD_PREFIXES = {
    "duong", "pho", "ngo", "hem", "ngach", "lo",
    "dai lo", "quoc lo", "tinh lo", "huong",
    "d.", "p.", "ng.",
};

// Loại bỏ prefix loại đường khỏi tên đường để search FTS5
static std::string stripRoadPrefix(const std::string& norm) {
    for (const auto& pfx : ROAD_PREFIXES) {
        if (norm.size() > pfx.size() + 1 &&
            norm.substr(0, pfx.size()) == pfx &&
            norm[pfx.size()] == ' ') {
            return norm.substr(pfx.size() + 1);
        }
    }
    return norm;
}

//xu ly dau vao la toa do

static bool tryParseCoord(const std::string& s,
                           double& lat, double& lon) {
    // Regex: hai số thực cách nhau bởi phẩy hoặc space
    static const std::regex RE_COORD(
        R"(^\s*([+-]?\d+\.?\d*)\s*[,\s]\s*([+-]?\d+\.?\d*)\s*$)");
    std::smatch m;
    if (std::regex_match(s, m, RE_COORD)) {
        try {
            double a = std::stod(m[1].str());
            double b = std::stod(m[2].str());
            // Heuristic: lat ∈ [7, 24], lon ∈ [100, 110] cho Việt Nam
            //đảo chiều lat, lon cho đầu vào nếu nhập ngược
            if (a >= 7.0 && a <= 24.0 && b >= 100.0 && b <= 115.0) {
                lat = a; lon = b; return true;
            }
            if (b >= 7.0 && b <= 24.0 && a >= 100.0 && a <= 115.0) {
                lat = b; lon = a; return true;
            }
        } catch (...) {}    //neu có lỗi -> ko làm gì cả & trả về false
    }
    return false;
}

//tách số nhà
// Parse số nhà từ đầu chuỗi: "75 ..." / "Số 75 ..." / "75A ..."
// Trả về vị trí kết thúc (sau số nhà + suffix) trong chuỗi norm
static bool parseHouseNumber(const std::string& norm,
                              int& number, std::string& suffix,
                              size_t& end_pos) {
    size_t i = 0;
    // Bỏ prefix "so " hoặc "s. "
    if (norm.substr(0, 3) == "so ") i = 3;
    else if (norm.substr(0, 3) == "s. ") i = 3;

    if (i >= norm.size() || !std::isdigit(norm[i])) return false;

    // Đọc phần số
    size_t num_start = i;
    while (i < norm.size() && std::isdigit(norm[i])) ++i;
    number = std::stoi(norm.substr(num_start, i - num_start));

    // Đọc suffix chữ cái tùy chọn: "A", "B" ...
    suffix.clear();
    if (i < norm.size() && std::isalpha(norm[i]) && norm[i] != ' ') {
        suffix += norm[i++];
    }

    // Phải có khoảng trắng sau, hoặc hết chuỗi
    if (i < norm.size() && norm[i] != ' ') return false;

    end_pos = i;    
    return true;
}

//loại bỏ prefix của đường: "đường 123" -> "123"
// Classify và extract thành phần hành chính từ 1 segment
// segment đã normalize (không dấu, lowercase)
static bool classifyAdminSegment(const std::string& seg_norm,
                                  std::string& ward,
                                  std::string& district,
                                  std::string& city) {
    // Thử match city alias trước
    auto city_it = CITY_ALIAS.find(seg_norm);
    if (city_it != CITY_ALIAS.end()) {
        city = city_it->second;
        return true;
    }

    // Thử match prefix hành chính
    for (const auto& [pfx, type] : ADMIN_PREFIX) {
        if (seg_norm.size() > pfx.size() &&
            seg_norm.substr(0, pfx.size()) == pfx) {
            std::string val = trim(seg_norm.substr(pfx.size()));
            if (val.empty()) continue;
            if (type == "ward")     { ward     = val; return true; }
            if (type == "district") { district = val; return true; }
            if (type == "city")     {
                // Thử city alias sau khi strip prefix
                auto ci = CITY_ALIAS.find(val);
                city = (ci != CITY_ALIAS.end()) ? ci->second : val;
                return true;
            }
        }
    }
    return false;
}

//main
AddressQuery parseAddressQuery(const std::string& input) {
    AddressQuery q;
    std::string s = trim(input);
    if (s.empty()) return q;

    
    // 1. kiểm tra đầu vào = tọa độ ? 
    if (tryParseCoord(s, q.coord_lat, q.coord_lon)) {
        q.is_coord = true;
        return q;
    }

    // 2. Tách theo dấu phẩy thành segments
    std::vector<std::string> segments = splitBy(s, ',');

    // Segment đầu tiên là phần chứa số nhà + tên đường
    // Các segment sau là ward, district, city
    std::string main_part = segments.empty() ? s : segments[0];
    std::string main_norm = normalizeVietnamese(main_part);

    // 3. Parse số nhà từ main_part
    size_t rest_start = 0;
    {
        int num; std::string suf; size_t ep;
        if (parseHouseNumber(main_norm, num, suf, ep)) {
            q.house_number = num;
            q.house_suffix = suf;
            rest_start = ep;
            // Bỏ khoảng trắng dẫn đầu
            while (rest_start < main_norm.size() && main_norm[rest_start] == ' ')
                ++rest_start;
        }
    }

    // 4. Phần còn lại sau số nhà là tên đường
    //    Hoặc nếu không có số nhà, tách ngõ/hẻm theo số
    //    VD: "Ngõ 45 Đường Láng" → ngõ=45, street=Đường Láng
    std::string street_raw = trim(main_norm.substr(rest_start));

    // Handle "ngo X duong Y" / "hem X duong Y"
    static const std::regex RE_NGO(
        R"((ngo|hem|ngach|lo)\s+(\d+[a-z]?)\s+(.+))");
    std::smatch m_ngo;
    if (std::regex_match(street_raw, m_ngo, RE_NGO)) {
        // Giữ nguyên tên đường (phần sau ngõ)
        street_raw = trim(m_ngo[3].str());
    }

    // Loại bỏ prefix loại đường khi lưu vào .street (để FTS search linh hoạt hơn)
    q.street = trim(street_raw);  // giữ cả prefix để hiển thị
    // (stripped version dùng khi query FTS, không lưu ở đây)

    // 5. Parse các segment hành chính (ward, district, city)
    for (size_t i = 1; i < segments.size(); ++i) {
        std::string seg_norm = normalizeVietnamese(segments[i]);
        classifyAdminSegment(seg_norm, q.ward, q.district, q.city);
    }

    // 6. Nếu chỉ có 1 segment (không có dấu phẩy):
    //    Thử detect tên TP ở cuối chuỗi
    //    VD: "cafe ha noi" → city=ha noi
    if (segments.size() == 1 && q.city.empty()) {
        for (const auto& [alias, city_norm] : CITY_ALIAS) {
            if (main_norm.size() >= alias.size()) {
                std::string suffix_part =
                    main_norm.substr(main_norm.size() - alias.size());
                if (suffix_part == alias) {
                    q.city   = city_norm;
                    q.street = trim(main_norm.substr(0, main_norm.size() - alias.size()));
                    break;
                }
            }
        }
    }

    return q;
}
