from __future__ import annotations

import csv
import json
import threading
from contextlib import asynccontextmanager
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import paho.mqtt.client as mqtt
from fastapi import FastAPI
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles


REPO_ROOT = Path(__file__).resolve().parents[1]
BADGE_ASSIGNMENTS = REPO_ROOT / "badge_assignments.csv"
DASHBOARD_DIR = REPO_ROOT / "dashboard"

MQTT_BROKER = "localhost"
MQTT_PORT = 1883

DEFAULT_ROOMS = [
    {"room_id": "CI-214", "room_name": "CI 214 - Firewall & IDS Lab"},
    {"room_id": "CI-210", "room_name": "CI 210 - Linux Fundamentals"},
    {"room_id": "CI-220", "room_name": "CI 220 - Lecture Hall"},
    {"room_id": "CI-205", "room_name": "CI 205 - Windows Security Lab"},
]


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def iso_now() -> str:
    return utc_now().isoformat()


def normalize_timestamp(raw: Any) -> int:
    try:
        value = int(raw)
    except (TypeError, ValueError):
        return int(utc_now().timestamp())

    # Scanner firmware may publish uptime-based seconds without RTC sync.
    if value < 2_000_000_000:
        return int(utc_now().timestamp())

    return value


def load_badge_assignments() -> dict[tuple[int, int], dict[str, str]]:
    assignments: dict[tuple[int, int], dict[str, str]] = {}
    with BADGE_ASSIGNMENTS.open(newline="") as csvfile:
        reader = csv.reader(csvfile, skipinitialspace=True)
        for row in reader:
            if not row or row[0].strip().startswith("#"):
                continue
            if len(row) < 5:
                continue

            major = int(row[0].strip())
            minor = int(row[1].strip())
            display_name = row[2].strip().strip('"')
            role = row[3].strip()
            notes = row[4].strip().strip('"')
            faculty_id = (
                display_name.lower().replace("dr. ", "").replace("prof. ", "").replace(" ", "_")
                if display_name
                else f"badge_{major}_{minor}"
            )
            assignments[(major, minor)] = {
                "faculty_id": faculty_id,
                "display_name": display_name or f"Badge {major}-{minor}",
                "role": role,
                "notes": notes,
            }
    return assignments


badge_registry = load_badge_assignments()
state_lock = threading.Lock()
scanner_status: dict[str, dict[str, Any]] = {}
rooms: dict[str, dict[str, Any]] = {
    room["room_id"]: {
        "room_id": room["room_id"],
        "room_name": room["room_name"],
        "scanner": {"status": "unknown", "last_seen": None, "ip": None},
        "present_faculty": {},
    }
    for room in DEFAULT_ROOMS
}
faculty_locations: dict[str, dict[str, Any]] = {}


def ensure_room(room_id: str, room_name: str | None = None) -> dict[str, Any]:
    room = rooms.get(room_id)
    if room is None:
        room = {
            "room_id": room_id,
            "room_name": room_name or room_id,
            "scanner": {"status": "unknown", "last_seen": None, "ip": None},
            "present_faculty": {},
        }
        rooms[room_id] = room
    elif room_name:
        room["room_name"] = room_name
    return room


def enrich_presence(payload: dict[str, Any]) -> dict[str, Any]:
    major = payload.get("major")
    minor = payload.get("minor")
    if major is None or minor is None:
        return payload

    try:
        key = (int(major), int(minor))
    except (TypeError, ValueError):
        return payload

    assignment = badge_registry.get(key)
    if not assignment:
        return payload

    payload.setdefault("faculty_id", assignment["faculty_id"])
    payload.setdefault("display_name", assignment["display_name"])
    payload.setdefault("role", assignment["role"])
    return payload


def handle_system_event(payload: dict[str, Any]) -> None:
    room_id = payload["room_id"]
    room = ensure_room(room_id, payload.get("room_name"))
    status = {
        "status": payload.get("status", "online"),
        "ip": payload.get("ip"),
        "last_seen": iso_now(),
    }
    room["scanner"] = status
    scanner_status[room_id] = status


def remove_from_other_rooms(faculty_id: str, keep_room_id: str) -> None:
    for room_id, room in rooms.items():
        if room_id == keep_room_id:
            continue
        room["present_faculty"].pop(faculty_id, None)


def handle_presence_event(payload: dict[str, Any]) -> None:
    payload = enrich_presence(payload)
    room_id = payload["room_id"]
    room_name = payload.get("room_name", room_id)
    room = ensure_room(room_id, room_name)
    room["scanner"]["last_seen"] = iso_now()

    faculty_id = payload.get("faculty_id") or f"badge_{payload.get('major', 'x')}_{payload.get('minor', 'x')}"
    display_name = payload.get("display_name", faculty_id)
    timestamp = normalize_timestamp(payload.get("timestamp"))
    present = bool(payload.get("present"))

    event = {
        "faculty_id": faculty_id,
        "display_name": display_name,
        "room_id": room_id,
        "room_name": room["room_name"],
        "present": present,
        "rssi": payload.get("rssi", 0),
        "timestamp": timestamp,
        "updated_at": iso_now(),
    }

    faculty_locations[faculty_id] = event

    if present:
        remove_from_other_rooms(faculty_id, room_id)
        room["present_faculty"][faculty_id] = event
    else:
        room["present_faculty"].pop(faculty_id, None)


def handle_message(topic: str, payload: dict[str, Any]) -> None:
    with state_lock:
        if topic.startswith("faculty/system/"):
            handle_system_event(payload)
        elif topic.startswith("faculty/presence/"):
            handle_presence_event(payload)


def mqtt_on_connect(client: mqtt.Client, userdata: Any, flags: dict[str, Any], rc: int) -> None:
    if rc == 0:
        client.subscribe("faculty/system/+")
        client.subscribe("faculty/presence/+")
    else:
        print(f"MQTT connect failed rc={rc}")


def mqtt_on_message(client: mqtt.Client, userdata: Any, message: mqtt.MQTTMessage) -> None:
    try:
        payload = json.loads(message.payload.decode("utf-8"))
    except json.JSONDecodeError:
        return

    handle_message(message.topic, payload)


mqtt_client = mqtt.Client(client_id="faculty-dashboard-backend")
mqtt_client.on_connect = mqtt_on_connect
mqtt_client.on_message = mqtt_on_message


@asynccontextmanager
async def lifespan(app: FastAPI):
    mqtt_client.connect(MQTT_BROKER, MQTT_PORT)
    mqtt_client.loop_start()
    try:
        yield
    finally:
        mqtt_client.loop_stop()
        mqtt_client.disconnect()


app = FastAPI(title="Faculty Presence Demo", lifespan=lifespan)
app.mount("/static", StaticFiles(directory=DASHBOARD_DIR), name="static")


@app.get("/")
def index() -> FileResponse:
    return FileResponse(DASHBOARD_DIR / "index.html")


@app.get("/api/state")
def api_state() -> dict[str, Any]:
    with state_lock:
        rooms_snapshot = []
        for room in rooms.values():
            present = list(room["present_faculty"].values())
            present.sort(key=lambda item: item["display_name"])
            rooms_snapshot.append(
                {
                    "room_id": room["room_id"],
                    "room_name": room["room_name"],
                    "scanner": room["scanner"],
                    "present_faculty": present,
                }
            )

        rooms_snapshot.sort(key=lambda item: item["room_name"])

        faculty_snapshot = list(faculty_locations.values())
        faculty_snapshot.sort(key=lambda item: item["display_name"])

        return {
            "generated_at": iso_now(),
            "rooms": rooms_snapshot,
            "faculty": faculty_snapshot,
        }


@app.get("/health")
def health() -> dict[str, str]:
    return {"status": "ok"}
