// Simulates a live AIS feed by generating synthetic vessels and pushing
// position updates to the spatial engine's REST API at a configurable rate.
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <vector>
#include <random>
#include <thread>
#include <chrono>
#include <cmath>
#include <string>

using json = nlohmann::json;

struct SimVessel {
    uint32_t    mmsi;
    double      lat, lon;
    float       speed_knots;
    float       heading_deg;
    std::string name;
    std::string ship_type;
};

static std::mt19937 rng{std::random_device{}()};

static double randf(double lo, double hi) {
    return std::uniform_real_distribution<double>{lo, hi}(rng);
}

static std::vector<SimVessel> generate_fleet(int n) {
    static const char* names[] = {
        "EVER GIVEN", "MSC OSCAR", "MAERSK MC-KINNEY", "COSCO SHIPPING",
        "CSCL GLOBE",  "OOCL HONG KONG", "MSC ZOE", "ULCC PIONEER",
        "NORDIC ORION", "SWIFT NAVIGATOR"
    };
    static const char* types[] = {
        "Cargo", "Tanker", "Container", "Bulk Carrier", "Fishing", "Passenger"
    };

    std::vector<SimVessel> fleet;
    fleet.reserve(n);
    for (int i = 0; i < n; ++i) {
        SimVessel v;
        v.mmsi        = 200000000u + i;
        v.lat         = randf(-60.0, 70.0);   // skip poles
        v.lon         = randf(-180.0, 180.0);
        v.speed_knots = static_cast<float>(randf(0.0, 20.0));
        v.heading_deg = static_cast<float>(randf(0.0, 360.0));
        v.name        = names[i % 10] + std::string(" ") + std::to_string(i);
        v.ship_type   = types[i % 6];
        fleet.push_back(v);
    }
    return fleet;
}

static void step(SimVessel& v, double dt_seconds) {
    // Move vessel along its heading at its speed.
    constexpr double KTS_TO_DEG_LAT_PER_SEC = 1.0 / (60.0 * 3600.0);
    double dist_deg = v.speed_knots * KTS_TO_DEG_LAT_PER_SEC * dt_seconds;
    double heading_rad = v.heading_deg * M_PI / 180.0;
    v.lat += dist_deg * std::cos(heading_rad);
    v.lon += dist_deg * std::sin(heading_rad) /
             std::max(std::cos(v.lat * M_PI / 180.0), 0.01);
    // Wrap longitude
    if (v.lon >  180.0) v.lon -= 360.0;
    if (v.lon < -180.0) v.lon += 360.0;
    // Clamp latitude
    v.lat = std::clamp(v.lat, -89.9, 89.9);
    // Drift heading slightly
    v.heading_deg = std::fmod(v.heading_deg + static_cast<float>(randf(-1.0, 1.0)) + 360.0f, 360.0f);
}

int main(int argc, char* argv[]) {
    int  n_vessels  = (argc > 1) ? std::atoi(argv[1]) : 1000;
    int  update_hz  = (argc > 2) ? std::atoi(argv[2]) : 10;
    int  server_port= (argc > 3) ? std::atoi(argv[3]) : 8080;

    std::cout << "Simulating " << n_vessels << " vessels at "
              << update_hz << " Hz → localhost:" << server_port << "\n";

    auto fleet = generate_fleet(n_vessels);
    httplib::Client cli("localhost", server_port);
    cli.set_connection_timeout(2);

    auto interval = std::chrono::milliseconds(1000 / update_hz);
    long long tick = 0;
    double dt = 1.0 / update_hz;

    while (true) {
        auto t0 = std::chrono::steady_clock::now();

        // Rotate which vessels we update this tick so we don't hammer the
        // server with n_vessels requests per tick at high fleet sizes.
        int batch = std::min(n_vessels, 200);
        int start = (tick * batch) % n_vessels;

        for (int i = 0; i < batch; ++i) {
            auto& v = fleet[(start + i) % n_vessels];
            step(v, dt);
            json body = {
                {"mmsi",        v.mmsi},
                {"lat",         v.lat},
                {"lon",         v.lon},
                {"speed_knots", v.speed_knots},
                {"heading_deg", v.heading_deg},
                {"name",        v.name},
                {"ship_type",   v.ship_type},
            };
            cli.Post("/vessels", body.dump(), "application/json");
        }

        if (tick % (update_hz * 5) == 0) {
            auto res = cli.Get("/stats");
            if (res) std::cout << "Stats: " << res->body << "\n";
        }

        ++tick;
        std::this_thread::sleep_until(t0 + interval);
    }
}
