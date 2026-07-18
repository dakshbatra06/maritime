#include "engine/spatial_engine.hpp"
#include "engine/anomaly.hpp"
#include <cassert>
#include <iostream>
#include <chrono>
#include <thread>

using namespace maritime;

static Vessel make_vessel(MMSI mmsi, double lat, double lon, float speed = 10.0f) {
    Vessel v;
    v.mmsi        = mmsi;
    v.lat         = lat;
    v.lon         = lon;
    v.speed_knots = speed;
    v.name        = "TEST-" + std::to_string(mmsi);
    v.last_seen   = std::chrono::steady_clock::now();
    return v;
}

static void test_upsert_and_get() {
    SpatialEngine engine;
    engine.upsert(make_vessel(123, 51.5, -0.1));  // near London
    auto v = engine.get(123);
    assert(v.has_value());
    assert(v->mmsi == 123);
    assert(v->lat == 51.5);
    std::cout << "PASS test_upsert_and_get\n";
}

static void test_remove() {
    SpatialEngine engine;
    engine.upsert(make_vessel(1, 0.0, 0.0));
    engine.remove(1);
    assert(!engine.get(1).has_value());
    assert(engine.size() == 0);
    std::cout << "PASS test_remove\n";
}

static void test_range_query() {
    SpatialEngine engine;
    engine.upsert(make_vessel(1,  51.5,  -0.1));  // London — inside
    engine.upsert(make_vessel(2,  48.8,   2.3));  // Paris  — outside
    engine.upsert(make_vessel(3,  51.45, -0.05)); // also inside

    BoundingBox bb{51.0, -1.0, 52.0, 1.0};
    auto results = engine.query_range(bb);
    assert(results.size() == 2);
    std::cout << "PASS test_range_query\n";
}

static void test_radius_query() {
    SpatialEngine engine;
    // Statue of Liberty: 40.6892° N, -74.0445° W
    engine.upsert(make_vessel(10,  40.6892, -74.0445));  // 0 nm away
    engine.upsert(make_vessel(11,  40.7484, -73.9967));  // ~5.3 nm — Empire State
    engine.upsert(make_vessel(12,  51.5,     -0.1));     // far away

    auto results = engine.query_radius({40.6892, -74.0445, 10.0});
    assert(results.size() == 2);
    assert(results[0].vessel.mmsi == 10);              // nearest first
    assert(results[0].distance_nm < results[1].distance_nm);
    std::cout << "PASS test_radius_query\n";
}

static void test_knn() {
    SpatialEngine engine;
    for (int i = 0; i < 20; ++i)
        engine.upsert(make_vessel(i, i * 0.5, i * 0.5));

    auto results = engine.query_knn({0.0, 0.0, 5});
    assert(results.size() == 5);
    for (size_t i = 1; i < results.size(); ++i)
        assert(results[i-1].distance_nm <= results[i].distance_nm);
    std::cout << "PASS test_knn\n";
}

static void test_update_position() {
    SpatialEngine engine;
    engine.upsert(make_vessel(42, 0.0, 0.0));
    // Move the vessel
    engine.upsert(make_vessel(42, 10.0, 10.0));
    assert(engine.size() == 1);  // no duplicate

    auto results = engine.query_range({9.0, 9.0, 11.0, 11.0});
    assert(results.size() == 1);
    assert(results[0].vessel.lat == 10.0);

    // Old position should yield no results
    auto old = engine.query_range({-1.0, -1.0, 1.0, 1.0});
    assert(old.empty());
    std::cout << "PASS test_update_position\n";
}

static void test_track_history() {
    SpatialEngine engine;
    // Insert the same vessel 5 times at different positions
    for (int i = 0; i < 5; ++i)
        engine.upsert(make_vessel(99, i * 1.0, i * 1.0));

    auto track = engine.get_track(99);
    assert(track.size() == 5);
    // Oldest point first
    assert(track.front().lat == 0.0);
    assert(track.back().lat  == 4.0);
    // Timestamps are non-decreasing
    for (size_t i = 1; i < track.size(); ++i)
        assert(track[i].timestamp >= track[i-1].timestamp);

    std::cout << "PASS test_track_history\n";
}

static void test_track_ring_buffer() {
    SpatialEngine engine;
    // Insert more than MAX_POINTS to verify ring behaviour
    for (int i = 0; i < 600; ++i)
        engine.upsert(make_vessel(77, i * 0.001, 0.0));

    auto track = engine.get_track(77);
    assert(track.size() == TrackStore::MAX_POINTS);
    // The oldest retained point should be the (600 - MAX_POINTS)th insertion
    double expected_lat = (600 - TrackStore::MAX_POINTS) * 0.001;
    assert(std::abs(track.front().lat - expected_lat) < 1e-9);
    std::cout << "PASS test_track_ring_buffer\n";
}

static void test_find_dark() {
    SpatialEngine engine;
    engine.upsert(make_vessel(1, 0.0, 0.0));
    engine.upsert(make_vessel(2, 1.0, 1.0));

    // Nothing should be dark immediately after insertion
    auto dark = engine.find_dark(std::chrono::seconds(5));
    assert(dark.empty());

    // After sleeping 1s, vessels silent for >=1s should appear
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    dark = engine.find_dark(std::chrono::seconds(1));
    assert(dark.size() == 2);
    for (auto& d : dark) assert(d.gap.count() >= 1);

    std::cout << "PASS test_find_dark\n";
}

static void test_anomaly_speed_jump() {
    std::vector<TrackPoint> track;
    auto now = WallClock::now();
    track.push_back({0.0, 0.0, 12.0f, 0.0f, now});
    track.push_back({0.1, 0.0, 80.0f, 0.0f, now + std::chrono::seconds(10)});

    auto anomalies = detect_anomalies(1, track);
    assert(!anomalies.empty());
    assert(anomalies[0].type == AnomalyType::SpeedJump);
    assert(anomalies[0].detail > 30.0);

    // Clean track should produce no speed-jump anomaly
    std::vector<TrackPoint> clean;
    clean.push_back({0.0, 0.0, 12.0f, 0.0f, now});
    clean.push_back({0.1, 0.0, 13.0f, 0.0f, now + std::chrono::seconds(60)});
    assert(detect_anomalies(2, clean).empty());

    std::cout << "PASS test_anomaly_speed_jump\n";
}

static void test_anomaly_teleportation() {
    auto now = WallClock::now();
    // 100 nm jump in 10 seconds → ~36000 knots implied
    std::vector<TrackPoint> track = {
        {0.0,   0.0, 12.0f, 0.0f, now},
        {0.0, 100.0, 12.0f, 0.0f, now + std::chrono::seconds(10)},
    };
    auto anomalies = detect_anomalies(3, track);
    assert(!anomalies.empty());
    assert(anomalies[0].type == AnomalyType::Teleportation);

    // Slow ship moving 1 nm over 10 minutes — fine
    std::vector<TrackPoint> fine = {
        {0.0, 0.0,     12.0f, 0.0f, now},
        {0.0, 0.01667, 12.0f, 0.0f, now + std::chrono::seconds(600)},
    };
    auto fine_anomalies = detect_anomalies(4, fine);
    bool has_teleport = false;
    for (auto& a : fine_anomalies)
        if (a.type == AnomalyType::Teleportation) has_teleport = true;
    assert(!has_teleport);

    std::cout << "PASS test_anomaly_teleportation\n";
}

static void test_anomaly_loitering() {
    auto now = WallClock::now();
    AnomalyConfig cfg;
    cfg.loiter_radius_nm   = 0.5;
    cfg.loiter_min_seconds = 10;  // short for testing

    // Build a track that circles within 0.1 nm for 15 seconds
    std::vector<TrackPoint> track;
    for (int i = 0; i < 10; ++i)
        track.push_back({0.0, 0.0001 * i, 2.0f, 0.0f,
                         now + std::chrono::seconds(i * 2)});

    auto anomalies = detect_anomalies(5, track, cfg);
    bool has_loiter = false;
    for (auto& a : anomalies)
        if (a.type == AnomalyType::Loitering) has_loiter = true;
    assert(has_loiter);

    std::cout << "PASS test_anomaly_loitering\n";
}

static void test_engine_check_vessel() {
    SpatialEngine engine;
    auto now = WallClock::now();
    // Insert a vessel with a teleportation event
    Vessel v1 = make_vessel(99, 0.0, 0.0);
    v1.last_seen = std::chrono::steady_clock::now();
    engine.upsert(v1);

    // Manually craft a second point far away to trigger teleportation
    // by calling upsert again 1 second later with a distant position
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Vessel v2 = make_vessel(99, 0.0, 100.0);  // 100° lon jump
    engine.upsert(v2);

    auto anomalies = engine.check_vessel(99);
    bool has_teleport = false;
    for (auto& a : anomalies)
        if (a.type == AnomalyType::Teleportation) has_teleport = true;
    assert(has_teleport);

    std::cout << "PASS test_engine_check_vessel\n";
}

static void bench_upsert(int n) {
    SpatialEngine engine;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < n; ++i)
        engine.upsert(make_vessel(i,
            -60.0 + (i % 1500) * 0.1,
            -180.0 + (i % 3600) * 0.1));
    auto t1 = std::chrono::high_resolution_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "BENCH upsert " << n << " vessels: " << ms << " ms ("
              << ms / n * 1000 << " µs/vessel)\n";
}

static void bench_radius_query(int n_vessels) {
    SpatialEngine engine;
    for (int i = 0; i < n_vessels; ++i)
        engine.upsert(make_vessel(i,
            -60.0 + (i % 1500) * 0.1,
            -180.0 + (i % 3600) * 0.1));

    constexpr int N_QUERIES = 1000;
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < N_QUERIES; ++i)
        engine.query_radius({0.0, 0.0, 50.0});
    auto t1 = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "BENCH radius query (" << n_vessels << " vessels, "
              << N_QUERIES << " queries): "
              << total_ms / N_QUERIES * 1000 << " µs/query\n";
}

int main() {
    test_upsert_and_get();
    test_remove();
    test_range_query();
    test_radius_query();
    test_knn();
    test_update_position();
    test_track_history();
    test_track_ring_buffer();
    test_find_dark();
    test_anomaly_speed_jump();
    test_anomaly_teleportation();
    test_anomaly_loitering();
    test_engine_check_vessel();

    bench_upsert(50000);
    bench_radius_query(50000);

    std::cout << "\nAll tests passed.\n";
    return 0;
}
