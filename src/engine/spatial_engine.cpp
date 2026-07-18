#include "engine/spatial_engine.hpp"
#include "engine/anomaly.hpp"
#include <cmath>

namespace maritime {

void SpatialEngine::upsert(Vessel v) {
    tracks_.record(v.mmsi, v);          // record before the move
    std::unique_lock lock(mtx_);
    index_.upsert(v.mmsi, v.lat, v.lon);
    vessels_[v.mmsi] = std::move(v);
}

void SpatialEngine::remove(MMSI mmsi) {
    tracks_.remove(mmsi);
    std::unique_lock lock(mtx_);
    index_.remove(mmsi);
    vessels_.erase(mmsi);
}

std::vector<QueryResult> SpatialEngine::query_range(const BoundingBox& bb) const {
    std::shared_lock lock(mtx_);
    auto candidates = index_.query_bbox(bb);

    std::vector<QueryResult> out;
    out.reserve(candidates.size());
    for (MMSI m : candidates) {
        auto it = vessels_.find(m);
        if (it == vessels_.end()) continue;
        const Vessel& v = it->second;
        if (bb.contains(v.lat, v.lon))
            out.push_back({v, 0.0});
    }
    return out;
}

std::vector<QueryResult> SpatialEngine::query_radius(const RadiusQuery& q) const {
    std::shared_lock lock(mtx_);
    auto bb = radius_to_bbox(q.center_lat, q.center_lon, q.radius_nm);
    auto candidates = index_.query_bbox(bb);

    std::vector<QueryResult> out;
    for (MMSI m : candidates) {
        auto it = vessels_.find(m);
        if (it == vessels_.end()) continue;
        const Vessel& v = it->second;
        double dist = haversine_nm(q.center_lat, q.center_lon, v.lat, v.lon);
        if (dist <= q.radius_nm)
            out.push_back({v, dist});
    }
    std::sort(out.begin(), out.end(),
              [](const QueryResult& a, const QueryResult& b) {
                  return a.distance_nm < b.distance_nm;
              });
    return out;
}

std::vector<QueryResult> SpatialEngine::query_knn(const KNNQuery& q) const {
    double radius = 50.0;
    std::vector<QueryResult> out;
    for (int attempts = 0; attempts < 8 && static_cast<int>(out.size()) < q.k; ++attempts) {
        out = query_radius({q.center_lat, q.center_lon, radius});
        radius *= 2.0;
    }
    if (static_cast<int>(out.size()) > q.k)
        out.resize(q.k);
    return out;
}

std::optional<Vessel> SpatialEngine::get(MMSI mmsi) const {
    std::shared_lock lock(mtx_);
    auto it = vessels_.find(mmsi);
    if (it == vessels_.end()) return std::nullopt;
    return it->second;
}

std::vector<TrackPoint> SpatialEngine::get_track(MMSI mmsi) const {
    return tracks_.get_track(mmsi);
}

std::vector<DarkVessel> SpatialEngine::find_dark(std::chrono::seconds threshold) const {
    return tracks_.find_dark(threshold);
}

std::vector<Anomaly> SpatialEngine::check_vessel(MMSI mmsi,
                                                  const AnomalyConfig& cfg) const {
    return detect_anomalies(mmsi, tracks_.get_track(mmsi), cfg);
}

std::vector<Anomaly> SpatialEngine::scan_anomalies(const AnomalyConfig& cfg) const {
    std::vector<MMSI> all_mmsi;
    {
        std::shared_lock lock(mtx_);
        all_mmsi.reserve(vessels_.size());
        for (auto& [m, _] : vessels_) all_mmsi.push_back(m);
    }
    std::vector<Anomaly> out;
    for (MMSI m : all_mmsi) {
        auto found = check_vessel(m, cfg);
        out.insert(out.end(), found.begin(), found.end());
    }
    return out;
}

size_t SpatialEngine::size() const {
    std::shared_lock lock(mtx_);
    return vessels_.size();
}

BoundingBox SpatialEngine::radius_to_bbox(double lat, double lon, double radius_nm) {
    constexpr double NM_PER_DEG_LAT = 60.0;
    double dlat = radius_nm / NM_PER_DEG_LAT;
    double dlon = radius_nm / (NM_PER_DEG_LAT * std::cos(lat * M_PI / 180.0));
    return {lat - dlat, lon - dlon, lat + dlat, lon + dlon};
}

} // namespace maritime
