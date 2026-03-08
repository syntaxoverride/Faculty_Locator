#!/usr/bin/env python3
"""
Badge Simulator — Test the backend without hardware.

Simulates faculty badges entering and leaving rooms by publishing
MQTT messages. Useful for:
  - Testing the backend before ESP32 hardware arrives
  - Demo'ing the dashboard to stakeholders
  - Student testing during lab development

Usage:
  pip install paho-mqtt
  python simulate_badges.py

  # Or with custom broker:
  python simulate_badges.py --broker 192.168.1.100
"""

import json
import time
import random
import argparse
from datetime import datetime, timezone

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("Install paho-mqtt: pip install paho-mqtt")
    exit(1)

# Simulated rooms
ROOMS = [
    {"room_id": "CI-214", "room_name": "CI 214 - Firewall & IDS Lab"},
    {"room_id": "CI-210", "room_name": "CI 210 - Linux Fundamentals"},
    {"room_id": "CI-220", "room_name": "CI 220 - Lecture Hall"},
    {"room_id": "CI-205", "room_name": "CI 205 - Windows Security Lab"},
]

# Simulated faculty
FACULTY = [
    {"faculty_id": "smith",  "display_name": "Dr. Smith"},
    {"faculty_id": "jones",  "display_name": "Dr. Jones"},
    {"faculty_id": "garcia", "display_name": "Prof. Garcia"},
    {"faculty_id": "chen",   "display_name": "Dr. Chen"},
    {"faculty_id": "patel",  "display_name": "Prof. Patel"},
]

# Track current state
faculty_locations = {}  # faculty_id -> room_id or None


def publish_system_status(client, room):
    """Announce a scanner as online."""
    payload = {
        "room_id": room["room_id"],
        "room_name": room["room_name"],
        "status": "online",
        "ip": f"192.168.1.{random.randint(50, 99)}",
    }
    topic = f"faculty/system/{room['room_id']}"
    client.publish(topic, json.dumps(payload), retain=True)
    print(f"  [SYSTEM] Scanner {room['room_id']} online")


def publish_presence(client, faculty, room, present):
    """Publish a presence event."""
    payload = {
        "room_id": room["room_id"],
        "room_name": room["room_name"],
        "faculty_id": faculty["faculty_id"],
        "display_name": faculty["display_name"],
        "present": present,
        "rssi": random.randint(-78, -62) if present else 0,
        "timestamp": int(time.time()),
    }
    topic = f"faculty/presence/{room['room_id']}"
    client.publish(topic, json.dumps(payload), retain=True)

    status = "ENTERED" if present else "LEFT"
    print(f"  [{status}] {faculty['display_name']} -> {room['room_name']}")


def simulate_event(client):
    """Simulate one random presence change."""
    faculty = random.choice(FACULTY)
    fid = faculty["faculty_id"]
    current_room = faculty_locations.get(fid)

    if current_room is None:
        # Faculty is not in any room — have them enter one
        room = random.choice(ROOMS)
        publish_presence(client, faculty, room, True)
        faculty_locations[fid] = room["room_id"]
    else:
        # Faculty is in a room — 60% chance they leave, 40% they move to another room
        # First, publish them leaving current room
        current = next(r for r in ROOMS if r["room_id"] == current_room)
        publish_presence(client, faculty, current, False)

        if random.random() < 0.4:
            # Move to a different room
            other_rooms = [r for r in ROOMS if r["room_id"] != current_room]
            new_room = random.choice(other_rooms)
            time.sleep(0.5)
            publish_presence(client, faculty, new_room, True)
            faculty_locations[fid] = new_room["room_id"]
        else:
            # Just leave
            faculty_locations[fid] = None


def main():
    parser = argparse.ArgumentParser(description="Simulate BLE badge presence events")
    parser.add_argument("--broker", default="localhost", help="MQTT broker address")
    parser.add_argument("--port", type=int, default=1883, help="MQTT broker port")
    parser.add_argument("--interval", type=float, default=3.0,
                        help="Seconds between simulated events")
    args = parser.parse_args()

    client = mqtt.Client(client_id="badge-simulator")
    print(f"Connecting to MQTT broker at {args.broker}:{args.port}...")
    client.connect(args.broker, args.port)
    client.loop_start()
    print("Connected!\n")

    # Announce all scanners as online
    print("Bringing scanners online:")
    for room in ROOMS:
        publish_system_status(client, room)
    print()

    # Start some faculty in rooms
    print("Initial placement:")
    for faculty in FACULTY[:3]:
        room = random.choice(ROOMS)
        publish_presence(client, faculty, room, True)
        faculty_locations[faculty["faculty_id"]] = room["room_id"]
    print()

    # Run simulation loop
    print(f"Simulating events every {args.interval}s (Ctrl+C to stop)...\n")
    try:
        while True:
            time.sleep(args.interval)
            simulate_event(client)
    except KeyboardInterrupt:
        print("\n\nStopping simulator...")
        client.loop_stop()
        client.disconnect()
        print("Done.")


if __name__ == "__main__":
    main()
