"""Typed HTTP client for the maritime C++ spatial engine."""
from __future__ import annotations

import httpx

ANOMALY_TYPE_NAMES = {0: "SpeedJump", 1: "Teleportation", 2: "Loitering"}

NAV_STATUS_NAMES = {
    0: "UnderWayUsingEngine",
    1: "AtAnchor",
    2: "NotUnderCommand",
    3: "RestrictedManeuverability",
    4: "ConstrainedByDraught",
    5: "Moored",
    6: "Aground",
    7: "EngagedInFishing",
    8: "UnderWaySailing",
    15: "Unknown",
}


class EngineClient:
    def __init__(self, base_url: str = "http://localhost:8080") -> None:
        self._http = httpx.Client(base_url=base_url, timeout=10.0)

    def close(self) -> None:
        self._http.close()

    def __enter__(self) -> "EngineClient":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    # --- vessel CRUD ---

    def get_vessel(self, mmsi: int) -> dict | None:
        r = self._http.get(f"/vessels/{mmsi}")
        if r.status_code == 404:
            return None
        r.raise_for_status()
        v = r.json()
        v["nav_status_name"] = NAV_STATUS_NAMES.get(v.get("nav_status", 15), "Unknown")
        return v

    def get_track(self, mmsi: int) -> list[dict]:
        r = self._http.get(f"/vessels/{mmsi}/track")
        if r.status_code == 404:
            return []
        r.raise_for_status()
        return r.json()

    # --- spatial queries ---

    def query_radius(self, lat: float, lon: float, radius_nm: float) -> list[dict]:
        r = self._http.get(
            "/query/radius",
            params={"lat": lat, "lon": lon, "radius_nm": radius_nm},
        )
        r.raise_for_status()
        return r.json()

    def query_range(
        self,
        min_lat: float,
        min_lon: float,
        max_lat: float,
        max_lon: float,
    ) -> list[dict]:
        r = self._http.get(
            "/query/range",
            params={
                "min_lat": min_lat,
                "min_lon": min_lon,
                "max_lat": max_lat,
                "max_lon": max_lon,
            },
        )
        r.raise_for_status()
        return r.json()

    def query_knn(self, lat: float, lon: float, k: int) -> list[dict]:
        r = self._http.get(
            "/query/knn",
            params={"lat": lat, "lon": lon, "k": k},
        )
        r.raise_for_status()
        return r.json()

    # --- anomaly detection ---

    def check_vessel_anomalies(self, mmsi: int) -> list[dict]:
        r = self._http.get(f"/anomalies/vessel/{mmsi}")
        r.raise_for_status()
        anomalies = r.json()
        for a in anomalies:
            a["type_name"] = ANOMALY_TYPE_NAMES.get(a.get("type", -1), "Unknown")
        return anomalies

    def scan_all_anomalies(self) -> list[dict]:
        r = self._http.get("/anomalies/scan")
        r.raise_for_status()
        anomalies = r.json()
        for a in anomalies:
            a["type_name"] = ANOMALY_TYPE_NAMES.get(a.get("type", -1), "Unknown")
        return anomalies

    def find_dark_vessels(self, min_gap_s: int = 600) -> list[dict]:
        r = self._http.get("/anomalies/dark", params={"min_gap_s": min_gap_s})
        r.raise_for_status()
        return r.json()

    # --- stats ---

    def get_stats(self) -> dict:
        r = self._http.get("/stats")
        r.raise_for_status()
        return r.json()
