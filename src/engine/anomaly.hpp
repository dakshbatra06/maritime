#pragma once
#include "track.hpp"
#include <string>
#include <vector>

namespace maritime {

enum class AnomalyType {
    SpeedJump,      // reported SOG changed implausibly between two updates
    Teleportation,  // position delta implies a speed no vessel can achieve
    Loitering,      // vessel circling a tight area for an extended period
};

struct Anomaly {
    AnomalyType  type;
    MMSI         mmsi;
    std::string  description;
    WallTime     detected_at;
    double       lat, lon;      // position where anomaly was detected
    double       detail;        // knots for speed anomalies, nm radius for loitering
};

struct AnomalyConfig {
    // SpeedJump: flag if SOG changes by more than this between consecutive points
    float  max_sog_delta_knots   = 30.0f;

    // Teleportation: flag if implied speed (distance/time) exceeds this
    double max_implied_speed_knots = 60.0;

    // Loitering: flag if vessel stays within radius_nm for at least min_duration
    double loiter_radius_nm      = 0.5;
    int    loiter_min_seconds    = 1800;  // 30 minutes
};

// Run all detectors against a track. Returns every anomaly found.
std::vector<Anomaly> detect_anomalies(MMSI mmsi,
                                      const std::vector<TrackPoint>& track,
                                      const AnomalyConfig& cfg = {});

} // namespace maritime
