#pragma once
#include <string>

// Address Query Parser
struct AddressQuery {
    int         house_number = -1;    // Giá trị -1 biểu thị địa chỉ không có số nhà
    std::string house_suffix;         // Phần hậu tố của số nhà (ví dụ: "A", "B" trong "50A", "125B")
    std::string street;               // tên đường (đã normalize)
    std::string ward;                 // phường/xã (đã normalize)
    std::string district;             // quận/huyện (đã normalize)
    std::string city;                 // tỉnh/thành phố (đã normalize)

    bool   is_coord  = false;         // true nếu input là tọa độ "21.03, 105.84"
    double coord_lat = 0.0;
    double coord_lon = 0.0;

    bool hasHouseNumber() const { return house_number >= 0; }
    bool hasStreet()      const { return !street.empty(); }
    bool isCoordinate()   const { return is_coord; }

    // True nếu đủ thông tin để thực hiện address interpolation
    bool canInterpolate()  const { return hasHouseNumber() && hasStreet(); }
};

// Bỏ dấu tiếng Việt và chuyển thành lowercase ASCII.
// "Hồ Gươm" → "ho guom", "Đường Láng" → "duong lang"
std::string normalizeVietnamese(const std::string& s);

// Phân tích chuỗi địa chỉ tiếng Việt thành AddressQuery.
// Handle đầy đủ các format:
//   "75 Phố A"
//   "Phố A 75"
//   "75A Đường Láng"
//   "Số 12 Ngõ 45 Đường Láng, P. Láng Thượng, Q. Đống Đa, HN"
//   "cafe đường láng"
//   "Trường THPT Chu Văn An"
//   "21.0278, 105.8342"
//   "21.0278 105.8342"
//   "TP. HCM" / "HN" / "Hà Nội"
AddressQuery parseAddressQuery(const std::string& input);
