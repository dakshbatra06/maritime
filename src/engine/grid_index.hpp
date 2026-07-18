#pragma once
#include "vessel.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>

namespace maritime {

// Haversine distance in nautical miles between two lat/lon points.
inline double haversine_nm(double lat1, double lon1, double lat2, double lon2) noexcept {
    constexpr double R_nm = 3440.065;  // Earth radius in nautical miles
    constexpr double DEG  = M_PI / 180.0;
    double dlat = (lat2 - lat1) * DEG;
    double dlon = (lon2 - lon1) * DEG;
    double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(lat1 * DEG) * std::cos(lat2 * DEG) *
               std::sin(dlon / 2) * std::sin(dlon / 2);
    return 2.0 * R_nm * std::asin(std::sqrt(a));
}

// World divided into cells of CELL_DEG x CELL_DEG degrees.
// At 0.1° that's roughly 6 nautical miles per cell at the equator —
// coarse enough to keep the cell count manageable, fine enough that a
// 5 nm radius query touches at most ~9 cells.
class GridIndex {
public:
    static constexpr double CELL_DEG = 0.1;

    void upsert(MMSI mmsi, double lat, double lon) {
        auto [cx, cy] = cell(lat, lon);
        auto key = pack(cx, cy);

        auto it = mmsi_to_cell_.find(mmsi);
        if (it != mmsi_to_cell_.end()) {
            if (it->second == key) return;           // same cell, no move needed
            cells_[it->second].erase(mmsi);
            it->second = key;
        } else {
            mmsi_to_cell_[mmsi] = key;
        }
        cells_[key].insert(mmsi);
    }

    void remove(MMSI mmsi) {
        auto it = mmsi_to_cell_.find(mmsi);
        if (it == mmsi_to_cell_.end()) return;
        cells_[it->second].erase(mmsi);
        mmsi_to_cell_.erase(it);
    }

    // Returns MMSIs whose grid cells overlap the bounding box.
    // Callers should re-check exact containment against vessel.lat/lon.
    std::vector<MMSI> query_bbox(const BoundingBox& bb) const {
        auto [x0, y0] = cell(bb.min_lat, bb.min_lon);
        auto [x1, y1] = cell(bb.max_lat, bb.max_lon);

        std::vector<MMSI> result;
        for (int cx = x0; cx <= x1; ++cx) {
            for (int cy = y0; cy <= y1; ++cy) {
                auto it = cells_.find(pack(cx, cy));
                if (it == cells_.end()) continue;
                for (MMSI m : it->second) result.push_back(m);
            }
        }
        return result;
    }

    size_t vessel_count() const noexcept { return mmsi_to_cell_.size(); }

private:
    // Cell coordinate from lat/lon
    static std::pair<int,int> cell(double lat, double lon) noexcept {
        int cx = static_cast<int>(std::floor((lon + 180.0) / CELL_DEG));
        int cy = static_cast<int>(std::floor((lat  +  90.0) / CELL_DEG));
        return {cx, cy};
    }

    // Pack two 16-bit cell coords into a 32-bit key
    static uint32_t pack(int cx, int cy) noexcept {
        return (static_cast<uint32_t>(cx + 1800) << 16) |
                static_cast<uint32_t>(cy +  900);
    }

    std::unordered_map<uint32_t, std::unordered_set<MMSI>> cells_;
    std::unordered_map<MMSI, uint32_t>                     mmsi_to_cell_;
};

} // namespace maritime
