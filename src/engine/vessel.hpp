#pragma once
#include <cstdint>
#include <string>
#include <chrono>

namespace maritime {

using MMSI = uint32_t;
using Timestamp = std::chrono::steady_clock::time_point;

enum class NavigationStatus : uint8_t {
    UnderWayEngine = 0,
    AtAnchor        = 1,
    NotUnderCommand = 2,
    Moored          = 5,
    Unknown         = 15,
};

struct Vessel {
    MMSI     mmsi        = 0;
    double   lat         = 0.0;   // degrees, -90..90
    double   lon         = 0.0;   // degrees, -180..180
    float    speed_knots = 0.0f;  // SOG
    float    heading_deg = 0.0f;  // COG, 0..360
    float    rot         = 0.0f;  // rate of turn deg/min
    std::string name;
    std::string ship_type;
    NavigationStatus nav_status = NavigationStatus::Unknown;
    Timestamp last_seen{};
};

struct BoundingBox {
    double min_lat, min_lon;
    double max_lat, max_lon;

    bool contains(double lat, double lon) const noexcept {
        return lat >= min_lat && lat <= max_lat &&
               lon >= min_lon && lon <= max_lon;
    }
};

struct RadiusQuery {
    double center_lat;
    double center_lon;
    double radius_nm;   // nautical miles
};

struct KNNQuery {
    double center_lat;
    double center_lon;
    int    k;
};

} // namespace maritime
