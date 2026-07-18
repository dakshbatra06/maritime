"""Tool definitions and dispatch for the maritime intelligence agent.

Each tool maps to one or more engine client calls. Tool results are returned as
plain strings so Claude can read them directly in the conversation.
"""
from __future__ import annotations

import json
from typing import Any

from engine_client import EngineClient

# --- Tool schema definitions (JSON Schema for the Anthropic API) ---

TOOL_DEFINITIONS: list[dict] = [
    {
        "name": "get_vessel_info",
        "description": (
            "Get the current position, speed, heading, name, and navigation status "
            "of a specific vessel by its MMSI number. Returns None if the vessel "
            "is not currently tracked."
        ),
        "input_schema": {
            "type": "object",
            "properties": {
                "mmsi": {
                    "type": "integer",
                    "description": "9-digit Maritime Mobile Service Identity number",
                }
            },
            "required": ["mmsi"],
        },
    },
    {
        "name": "get_vessel_track",
        "description": (
            "Get the position history (track) for a vessel. Returns a summary with "
            "the oldest point, newest point, and middle point, plus total track length. "
            "Useful for understanding a vessel's movement pattern over time."
        ),
        "input_schema": {
            "type": "object",
            "properties": {
                "mmsi": {
                    "type": "integer",
                    "description": "MMSI number of the vessel",
                }
            },
            "required": ["mmsi"],
        },
    },
    {
        "name": "find_vessels_in_radius",
        "description": (
            "Find all currently tracked vessels within a given radius (nautical miles) "
            "of a geographic point. Returns vessels sorted by distance, nearest first. "
            "Use this to find vessels near a suspicious location or near another vessel."
        ),
        "input_schema": {
            "type": "object",
            "properties": {
                "lat": {"type": "number", "description": "Center latitude in decimal degrees"},
                "lon": {"type": "number", "description": "Center longitude in decimal degrees"},
                "radius_nm": {
                    "type": "number",
                    "description": "Search radius in nautical miles",
                },
            },
            "required": ["lat", "lon", "radius_nm"],
        },
    },
    {
        "name": "find_vessels_in_bbox",
        "description": (
            "Find all currently tracked vessels within a geographic bounding box. "
            "Useful for sweeping a specific sea area (e.g. a strait, bay, or EEZ boundary)."
        ),
        "input_schema": {
            "type": "object",
            "properties": {
                "min_lat": {"type": "number", "description": "Southern boundary latitude"},
                "min_lon": {"type": "number", "description": "Western boundary longitude"},
                "max_lat": {"type": "number", "description": "Northern boundary latitude"},
                "max_lon": {"type": "number", "description": "Eastern boundary longitude"},
            },
            "required": ["min_lat", "min_lon", "max_lat", "max_lon"],
        },
    },
    {
        "name": "find_nearest_vessels",
        "description": (
            "Find the k nearest vessels to a geographic point. "
            "Use this to understand what other vessels are close to a vessel of interest."
        ),
        "input_schema": {
            "type": "object",
            "properties": {
                "lat": {"type": "number", "description": "Reference latitude"},
                "lon": {"type": "number", "description": "Reference longitude"},
                "k": {
                    "type": "integer",
                    "description": "Number of nearest vessels to return",
                    "minimum": 1,
                    "maximum": 20,
                },
            },
            "required": ["lat", "lon", "k"],
        },
    },
    {
        "name": "check_vessel_anomalies",
        "description": (
            "Run all anomaly detectors against a specific vessel's track history. "
            "Detects: SpeedJump (reported speed delta > 30 kn), Teleportation "
            "(implied speed between two positions > 60 kn — possible GPS spoofing), "
            "and Loitering (vessel remains within 0.5 nm for 30+ minutes). "
            "Returns an empty list if no anomalies are found."
        ),
        "input_schema": {
            "type": "object",
            "properties": {
                "mmsi": {"type": "integer", "description": "MMSI number of the vessel"}
            },
            "required": ["mmsi"],
        },
    },
    {
        "name": "scan_all_anomalies",
        "description": (
            "Run all anomaly detectors across every tracked vessel simultaneously. "
            "Returns a summary grouped by anomaly type plus the raw anomaly list (up to 50). "
            "Use this for an initial fleet-wide threat survey."
        ),
        "input_schema": {
            "type": "object",
            "properties": {},
            "required": [],
        },
    },
    {
        "name": "find_dark_vessels",
        "description": (
            "Find vessels that have stopped transmitting AIS ('gone dark') for at least "
            "the specified number of minutes. AIS gaps can indicate intentional disabling "
            "of transponders to avoid detection — a key maritime red flag."
        ),
        "input_schema": {
            "type": "object",
            "properties": {
                "min_gap_minutes": {
                    "type": "integer",
                    "description": "Minimum AIS silence duration in minutes (default: 10)",
                    "minimum": 1,
                    "default": 10,
                }
            },
            "required": [],
        },
    },
    {
        "name": "get_fleet_stats",
        "description": (
            "Get high-level statistics about the currently tracked fleet "
            "(total vessel count). Use this first to understand the scope of the dataset."
        ),
        "input_schema": {
            "type": "object",
            "properties": {},
            "required": [],
        },
    },
]


# --- Tool dispatch ---

def dispatch(client: EngineClient, tool_name: str, tool_input: dict[str, Any]) -> str:
    """Execute a tool call and return the result as a string for Claude."""
    try:
        return _dispatch_inner(client, tool_name, tool_input)
    except Exception as exc:
        return f"Error executing {tool_name}: {exc}"


def _dispatch_inner(
    client: EngineClient, tool_name: str, tool_input: dict[str, Any]
) -> str:
    if tool_name == "get_vessel_info":
        vessel = client.get_vessel(tool_input["mmsi"])
        if vessel is None:
            return f"Vessel MMSI {tool_input['mmsi']} is not currently tracked."
        return json.dumps(vessel, indent=2)

    if tool_name == "get_vessel_track":
        track = client.get_track(tool_input["mmsi"])
        if not track:
            return f"No track history for MMSI {tool_input['mmsi']}."
        n = len(track)
        summary = {
            "mmsi": tool_input["mmsi"],
            "total_points": n,
            "oldest": track[0],
            "middle": track[n // 2] if n > 2 else None,
            "newest": track[-1],
            "duration_s": (track[-1]["ts_ms"] - track[0]["ts_ms"]) / 1000.0 if n > 1 else 0,
        }
        return json.dumps(summary, indent=2)

    if tool_name == "find_vessels_in_radius":
        results = client.query_radius(
            tool_input["lat"], tool_input["lon"], tool_input["radius_nm"]
        )
        if not results:
            return (
                f"No vessels found within {tool_input['radius_nm']} nm "
                f"of ({tool_input['lat']}, {tool_input['lon']})."
            )
        return json.dumps(
            {"count": len(results), "vessels": results[:20]}, indent=2
        )

    if tool_name == "find_vessels_in_bbox":
        results = client.query_range(
            tool_input["min_lat"],
            tool_input["min_lon"],
            tool_input["max_lat"],
            tool_input["max_lon"],
        )
        if not results:
            return "No vessels found in the specified bounding box."
        return json.dumps({"count": len(results), "vessels": results[:20]}, indent=2)

    if tool_name == "find_nearest_vessels":
        results = client.query_knn(
            tool_input["lat"], tool_input["lon"], tool_input["k"]
        )
        if not results:
            return "No vessels found."
        return json.dumps({"vessels": results}, indent=2)

    if tool_name == "check_vessel_anomalies":
        anomalies = client.check_vessel_anomalies(tool_input["mmsi"])
        if not anomalies:
            return f"No anomalies detected for MMSI {tool_input['mmsi']}."
        return json.dumps(
            {
                "mmsi": tool_input["mmsi"],
                "anomaly_count": len(anomalies),
                "anomalies": anomalies,
            },
            indent=2,
        )

    if tool_name == "scan_all_anomalies":
        anomalies = client.scan_all_anomalies()
        if not anomalies:
            return "No anomalies detected across the fleet."
        by_type: dict[str, list] = {}
        for a in anomalies:
            by_type.setdefault(a["type_name"], []).append(a)
        return json.dumps(
            {
                "total": len(anomalies),
                "by_type": {k: len(v) for k, v in by_type.items()},
                "anomalies": anomalies[:50],
            },
            indent=2,
        )

    if tool_name == "find_dark_vessels":
        gap_min = tool_input.get("min_gap_minutes", 10)
        dark = client.find_dark_vessels(min_gap_s=gap_min * 60)
        if not dark:
            return f"No vessels have gone dark for more than {gap_min} minutes."
        return json.dumps({"count": len(dark), "dark_vessels": dark}, indent=2)

    if tool_name == "get_fleet_stats":
        return json.dumps(client.get_stats(), indent=2)

    return f"Unknown tool: {tool_name}"
