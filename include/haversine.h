#pragma once
#include <cmath>

// Haversine formula: compute distance (meters) between two lat/lon coordinates
inline double haversine(double lat1, double lon1, double lat2, double lon2) {
    constexpr double PI = 3.14159265358979323846;
    constexpr double R = 6371000.0; // Earth radius in meters
    double dlat = (lat2 - lat1) * PI / 180.0;
    double dlon = (lon2 - lon1) * PI / 180.0;
    double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(lat1 * PI / 180.0) * std::cos(lat2 * PI / 180.0) *
               std::sin(dlon / 2) * std::sin(dlon / 2);
    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return R * c;
}

// Xấp xỉ Equirectangular: nhanh hơn Haversine (~4x), sai số < 0.1% cho khoảng cách ngắn (< 50km).
// Chỉ dùng 1 cos() + 1 sqrt() thay vì chuỗi sin/cos/atan2 của Haversine.
// Phù hợp để tính tổng chiều dài đường đi gồm hàng trăm đoạn ngắn liên tiếp.
inline double equirectangular_distance(double lat1, double lon1, double lat2, double lon2) {
    constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;
    constexpr double EARTH_RADIUS = 6371000.0;
    double lat1_rad = lat1 * DEG_TO_RAD;
    double lat2_rad = lat2 * DEG_TO_RAD;
    double x = (lon2 - lon1) * DEG_TO_RAD * std::cos((lat1_rad + lat2_rad) * 0.5);
    double y = lat2_rad - lat1_rad;
    return EARTH_RADIUS * std::sqrt(x * x + y * y);
}
