#pragma once
#include "vessel.hpp"
#include "grid_index.hpp"
#include "track.hpp"
#include "anomaly.hpp"
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <optional>
#include <algorithm>

namespace maritime {

struct QueryResult {
    Vessel vessel;
    double distance_nm;  // 0.0 for bbox queries
};

class SpatialEngine {
public:
    // Insert or update a vessel's full state.
    void upsert(Vessel v);

    // Remove a vessel (e.g. it went silent too long).
    void remove(MMSI mmsi);

    // Return all vessels whose positions fall within the bounding box.
    std::vector<QueryResult> query_range(const BoundingBox& bb) const;

    // Return all vessels within radius_nm nautical miles of the center point.
    std::vector<QueryResult> query_radius(const RadiusQuery& q) const;

    // Return the k nearest vessels to the center point.
    std::vector<QueryResult> query_knn(const KNNQuery& q) const;

    // Snapshot of one vessel by MMSI.
    std::optional<Vessel> get(MMSI mmsi) const;

    // Track history for one vessel, oldest-first.
    std::vector<TrackPoint> get_track(MMSI mmsi) const;

    // Vessels that haven't reported in at least `threshold`.
    std::vector<DarkVessel> find_dark(std::chrono::seconds threshold) const;

    // Run all anomaly detectors against one vessel's track.
    std::vector<Anomaly> check_vessel(MMSI mmsi,
                                      const AnomalyConfig& cfg = {}) const;

    // Scan every tracked vessel and return all anomalies found.
    std::vector<Anomaly> scan_anomalies(const AnomalyConfig& cfg = {}) const;

    // Total tracked vessels.
    size_t size() const;

private:
    mutable std::shared_mutex         mtx_;
    GridIndex                         index_;
    std::unordered_map<MMSI, Vessel>  vessels_;
    TrackStore                        tracks_;

    static BoundingBox radius_to_bbox(double lat, double lon, double radius_nm);
};

} // namespace maritime
