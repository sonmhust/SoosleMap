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
