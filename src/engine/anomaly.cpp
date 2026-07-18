#include "engine/anomaly.hpp"
#include "engine/grid_index.hpp"  // haversine_nm
#include <cmath>
#include <sstream>
#include <iomanip>

namespace maritime {

namespace {

double elapsed_seconds(WallTime a, WallTime b) {
    return std::chrono::duration<double>(b - a).count();
}

std::string fmt(double v, int prec = 1) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(prec) << v;
    return os.str();
}

// ── Speed jump ────────────────────────────────────────────────────────────────
// Compares reported SOG between consecutive points. A vessel that jumps from
// 12 knots to 80 knots in one AIS message has either spoofed its speed or
// the message was corrupted.
void detect_speed_jumps(MMSI mmsi,
                        const std::vector<TrackPoint>& track,
                        const AnomalyConfig& cfg,
                        std::vector<Anomaly>& out) {
    for (size_t i = 1; i < track.size(); ++i) {
        float delta = std::abs(track[i].speed_knots - track[i-1].speed_knots);
        if (delta > cfg.max_sog_delta_knots) {
            out.push_back({
                AnomalyType::SpeedJump, mmsi,
                "SOG jumped " + fmt(delta) + " kts (" +
                    fmt(track[i-1].speed_knots) + " → " +
                    fmt(track[i].speed_knots) + " kts)",
                track[i].timestamp,
                track[i].lat, track[i].lon,
                delta,
            });
        }
    }
}

// ── Teleportation ─────────────────────────────────────────────────────────────
// Computes the speed implied by (position delta / time delta). If that exceeds
// any vessel's physical maximum (fastest ships peak ~60 kts), the position
// was either spoofed or the MMSI is being reused by two vessels.
void detect_teleportation(MMSI mmsi,
                          const std::vector<TrackPoint>& track,
                          const AnomalyConfig& cfg,
                          std::vector<Anomaly>& out) {
    for (size_t i = 1; i < track.size(); ++i) {
        double dt = elapsed_seconds(track[i-1].timestamp, track[i].timestamp);
        if (dt <= 0.0) continue;

        double dist_nm  = haversine_nm(track[i-1].lat, track[i-1].lon,
                                       track[i].lat,   track[i].lon);
        double hours    = dt / 3600.0;
        double implied  = dist_nm / hours;

        if (implied > cfg.max_implied_speed_knots) {
            out.push_back({
                AnomalyType::Teleportation, mmsi,
                "Implied speed " + fmt(implied) + " kts over " +
                    fmt(dist_nm, 2) + " nm in " + fmt(dt, 0) + "s",
                track[i].timestamp,
                track[i].lat, track[i].lon,
                implied,
            });
        }
    }
}

// ── Loitering ─────────────────────────────────────────────────────────────────
// Scans the track with a sliding time window. If all points in the window
// fit inside loiter_radius_nm of the window's first point, the vessel is
// loitering. We advance the window start once the duration threshold is met
// to avoid reporting the same loiter repeatedly.
void detect_loitering(MMSI mmsi,
                      const std::vector<TrackPoint>& track,
                      const AnomalyConfig& cfg,
                      std::vector<Anomaly>& out) {
    if (track.size() < 2) return;

    size_t window_start = 0;
    bool   in_loiter    = false;

    for (size_t i = 1; i < track.size(); ++i) {
        double duration = elapsed_seconds(track[window_start].timestamp,
                                          track[i].timestamp);
        double radius   = haversine_nm(track[window_start].lat,
                                       track[window_start].lon,
                                       track[i].lat,
                                       track[i].lon);

        if (radius > cfg.loiter_radius_nm) {
            // Left the loiter zone — reset window
            window_start = i;
            in_loiter    = false;
            continue;
        }

        if (!in_loiter && duration >= cfg.loiter_min_seconds) {
            in_loiter = true;
            out.push_back({
                AnomalyType::Loitering, mmsi,
                "Loitering within " + fmt(cfg.loiter_radius_nm, 2) +
                    " nm for " + fmt(duration / 60.0, 0) + " min",
                track[i].timestamp,
                track[i].lat, track[i].lon,
                radius,
            });
        }
    }
}

} // namespace

std::vector<Anomaly> detect_anomalies(MMSI mmsi,
                                      const std::vector<TrackPoint>& track,
                                      const AnomalyConfig& cfg) {
    if (track.size() < 2) return {};
    std::vector<Anomaly> out;
    detect_speed_jumps(mmsi, track, cfg, out);
    detect_teleportation(mmsi, track, cfg, out);
    detect_loitering(mmsi, track, cfg, out);
    return out;
}

} // namespace maritime
