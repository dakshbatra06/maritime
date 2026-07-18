#pragma once
#include "vessel.hpp"
#include <deque>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <shared_mutex>

namespace maritime {

using WallClock  = std::chrono::system_clock;
using WallTime   = WallClock::time_point;

struct TrackPoint {
    double    lat, lon;
    float     speed_knots;
    float     heading_deg;
    WallTime  timestamp;
};

struct DarkVessel {
    MMSI     mmsi;
    WallTime last_seen;
    double   last_lat, last_lon;
    std::chrono::seconds gap;  // how long it's been silent
};

class TrackStore {
public:
    static constexpr size_t MAX_POINTS = 500;

    // Record a new position for a vessel.
    void record(MMSI mmsi, const Vessel& v) {
        std::unique_lock lock(mtx_);
        auto& dq = tracks_[mmsi];
        dq.push_back({v.lat, v.lon, v.speed_knots, v.heading_deg, WallClock::now()});
        if (dq.size() > MAX_POINTS)
            dq.pop_front();
        last_seen_[mmsi] = dq.back().timestamp;
    }

    // Remove all history for a vessel (e.g. after it's purged).
    void remove(MMSI mmsi) {
        std::unique_lock lock(mtx_);
        tracks_.erase(mmsi);
        last_seen_.erase(mmsi);
    }

    // Return the full track for one vessel, oldest-first.
    std::vector<TrackPoint> get_track(MMSI mmsi) const {
        std::shared_lock lock(mtx_);
        auto it = tracks_.find(mmsi);
        if (it == tracks_.end()) return {};
        return {it->second.begin(), it->second.end()};
    }

    // Return vessels that haven't reported in at least `threshold`.
    std::vector<DarkVessel> find_dark(std::chrono::seconds threshold) const {
        std::shared_lock lock(mtx_);
        auto now = WallClock::now();
        std::vector<DarkVessel> out;
        for (auto& [mmsi, ts] : last_seen_) {
            auto gap = std::chrono::duration_cast<std::chrono::seconds>(now - ts);
            if (gap >= threshold) {
                const auto& dq = tracks_.at(mmsi);
                out.push_back({mmsi, ts, dq.back().lat, dq.back().lon, gap});
            }
        }
        return out;
    }

    size_t vessel_count() const {
        std::shared_lock lock(mtx_);
        return tracks_.size();
    }

private:
    mutable std::shared_mutex                         mtx_;
    std::unordered_map<MMSI, std::deque<TrackPoint>> tracks_;
    std::unordered_map<MMSI, WallTime>               last_seen_;
};

} // namespace maritime
