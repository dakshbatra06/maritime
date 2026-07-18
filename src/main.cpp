#include "engine/spatial_engine.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>

using json = nlohmann::json;
using namespace maritime;

// ---------- serialization helpers ----------

static int64_t to_unix_ms(WallTime t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               t.time_since_epoch()).count();
}

static json track_point_to_json(const TrackPoint& p) {
    return {
        {"lat",         p.lat},
        {"lon",         p.lon},
        {"speed_knots", p.speed_knots},
        {"heading_deg", p.heading_deg},
        {"ts_ms",       to_unix_ms(p.timestamp)},
    };
}

static json vessel_to_json(const Vessel& v) {
    return {
        {"mmsi",        v.mmsi},
        {"lat",         v.lat},
        {"lon",         v.lon},
        {"speed_knots", v.speed_knots},
        {"heading_deg", v.heading_deg},
        {"name",        v.name},
        {"ship_type",   v.ship_type},
        {"nav_status",  static_cast<int>(v.nav_status)},
    };
}

static json result_to_json(const QueryResult& r) {
    auto j = vessel_to_json(r.vessel);
    j["distance_nm"] = r.distance_nm;
    return j;
}

static void send_error(httplib::Response& res, int code, std::string msg) {
    res.status = code;
    res.set_content(json{{"error", std::move(msg)}}.dump(), "application/json");
}

// ---------- main ----------

int main(int argc, char* argv[]) {
    SpatialEngine engine;
    httplib::Server svr;

    // ----- POST /vessels  — upsert a vessel -----
    svr.Post("/vessels", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            Vessel v;
            v.mmsi        = body.at("mmsi").get<MMSI>();
            v.lat         = body.at("lat").get<double>();
            v.lon         = body.at("lon").get<double>();
            v.speed_knots = body.value("speed_knots", 0.0f);
            v.heading_deg = body.value("heading_deg", 0.0f);
            v.rot         = body.value("rot", 0.0f);
            v.name        = body.value("name", "");
            v.ship_type   = body.value("ship_type", "");
            v.nav_status  = static_cast<NavigationStatus>(body.value("nav_status", 15));
            v.last_seen   = std::chrono::steady_clock::now();
            engine.upsert(std::move(v));
            res.status = 204;
        } catch (const std::exception& e) {
            send_error(res, 400, e.what());
        }
    });

    // ----- DELETE /vessels/:mmsi -----
    svr.Delete(R"(/vessels/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        MMSI mmsi = std::stoul(req.matches[1]);
        engine.remove(mmsi);
        res.status = 204;
    });

    // ----- GET /vessels/:mmsi -----
    svr.Get(R"(/vessels/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        MMSI mmsi = std::stoul(req.matches[1]);
        auto v = engine.get(mmsi);
        if (!v) { send_error(res, 404, "vessel not found"); return; }
        res.set_content(vessel_to_json(*v).dump(), "application/json");
    });

    // ----- GET /query/range?min_lat=&min_lon=&max_lat=&max_lon= -----
    svr.Get("/query/range", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            BoundingBox bb;
            bb.min_lat = std::stod(req.get_param_value("min_lat"));
            bb.min_lon = std::stod(req.get_param_value("min_lon"));
            bb.max_lat = std::stod(req.get_param_value("max_lat"));
            bb.max_lon = std::stod(req.get_param_value("max_lon"));
            auto results = engine.query_range(bb);
            json arr = json::array();
            for (auto& r : results) arr.push_back(result_to_json(r));
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception& e) {
            send_error(res, 400, e.what());
        }
    });

    // ----- GET /query/radius?lat=&lon=&radius_nm= -----
    svr.Get("/query/radius", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            RadiusQuery q;
            q.center_lat = std::stod(req.get_param_value("lat"));
            q.center_lon = std::stod(req.get_param_value("lon"));
            q.radius_nm  = std::stod(req.get_param_value("radius_nm"));
            auto results = engine.query_radius(q);
            json arr = json::array();
            for (auto& r : results) arr.push_back(result_to_json(r));
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception& e) {
            send_error(res, 400, e.what());
        }
    });

    // ----- GET /query/knn?lat=&lon=&k= -----
    svr.Get("/query/knn", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            KNNQuery q;
            q.center_lat = std::stod(req.get_param_value("lat"));
            q.center_lon = std::stod(req.get_param_value("lon"));
            q.k          = std::stoi(req.get_param_value("k"));
            auto results = engine.query_knn(q);
            json arr = json::array();
            for (auto& r : results) arr.push_back(result_to_json(r));
            res.set_content(arr.dump(), "application/json");
        } catch (const std::exception& e) {
            send_error(res, 400, e.what());
        }
    });

    // ----- GET /vessels/:mmsi/track -----
    svr.Get(R"(/vessels/(\d+)/track)", [&](const httplib::Request& req, httplib::Response& res) {
        MMSI mmsi = std::stoul(req.matches[1]);
        auto track = engine.get_track(mmsi);
        if (track.empty()) { send_error(res, 404, "no track for vessel"); return; }
        json arr = json::array();
        for (auto& p : track) arr.push_back(track_point_to_json(p));
        res.set_content(arr.dump(), "application/json");
    });

    // ----- GET /anomalies/vessel/:mmsi -----
    svr.Get(R"(/anomalies/vessel/(\d+))", [&](const httplib::Request& req, httplib::Response& res) {
        MMSI mmsi = std::stoul(req.matches[1]);
        auto anomalies = engine.check_vessel(mmsi);
        json arr = json::array();
        for (auto& a : anomalies) {
            arr.push_back({
                {"type",        static_cast<int>(a.type)},
                {"mmsi",        a.mmsi},
                {"description", a.description},
                {"lat",         a.lat},
                {"lon",         a.lon},
                {"detail",      a.detail},
                {"ts_ms",       to_unix_ms(a.detected_at)},
            });
        }
        res.set_content(arr.dump(), "application/json");
    });

    // ----- GET /anomalies/scan -----
    svr.Get("/anomalies/scan", [&](const httplib::Request& req, httplib::Response& res) {
        auto anomalies = engine.scan_anomalies();
        json arr = json::array();
        for (auto& a : anomalies) {
            arr.push_back({
                {"type",        static_cast<int>(a.type)},
                {"mmsi",        a.mmsi},
                {"description", a.description},
                {"lat",         a.lat},
                {"lon",         a.lon},
                {"detail",      a.detail},
                {"ts_ms",       to_unix_ms(a.detected_at)},
            });
        }
        res.set_content(arr.dump(), "application/json");
    });

    // ----- GET /anomalies/dark?min_gap_s=600 -----
    svr.Get("/anomalies/dark", [&](const httplib::Request& req, httplib::Response& res) {
        int gap_s = 600;  // default: 10 minutes
        if (req.has_param("min_gap_s"))
            gap_s = std::stoi(req.get_param_value("min_gap_s"));
        auto dark = engine.find_dark(std::chrono::seconds(gap_s));
        json arr = json::array();
        for (auto& d : dark) {
            arr.push_back({
                {"mmsi",       d.mmsi},
                {"last_lat",   d.last_lat},
                {"last_lon",   d.last_lon},
                {"last_seen_ms", to_unix_ms(d.last_seen)},
                {"gap_s",      d.gap.count()},
            });
        }
        res.set_content(arr.dump(), "application/json");
    });

    // ----- GET /stats -----
    svr.Get("/stats", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content(json{{"vessel_count", engine.size()}}.dump(), "application/json");
    });

    int port = (argc > 1) ? std::atoi(argv[1]) : 8080;
    std::cout << "Maritime spatial engine listening on port " << port << "\n";
    svr.listen("0.0.0.0", port);
}
