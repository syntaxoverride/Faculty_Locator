# Faculty Presence Backend

A lightweight backend powers a classroom faculty badge prototype designed to show who is present in each room.

Faculty badges broadcast BLE advertisements, room scanners detect those signals at the doorway, and the backend turns those raw sightings into live presence data for the dashboard.

Full deployment scales naturally to one scanner per room, allowing faculty presence to be monitored across a building in near real time.

The backend subscribes to MQTT events from either:

- `simulate_badges.py`
- `room_scanner/room_scanner.ino`

Current room presence is stored in memory and exposed through a simple dashboard.

## Run

1. Install dependencies:

```bash
python3 -m pip install -r backend/requirements.txt
```

2. Start the backend:

```bash
uvicorn backend.server:app --reload --host 0.0.0.0 --port 8000
```

3. Open the dashboard:

`http://localhost:8000`

## MQTT

The backend connects to an MQTT broker. By default it uses `localhost:1883`.

If the broker runs on another host (e.g. a VM at `192.168.1.100`), set:

```bash
MQTT_BROKER=192.168.1.100 uvicorn backend.server:app --host 0.0.0.0 --port 8000
```

The backend must connect to the same broker the scanner publishes to. If the scanner uses `MQTT_BROKER = "192.168.1.100"` in its firmware, the backend needs `MQTT_BROKER=192.168.1.100` when run on your Mac.

Topics:

- `faculty/system/<room_id>`
- `faculty/presence/<room_id>`

## Notes

- The simulator already publishes the expected payload shape.
- The scanner firmware publishes the same topics and includes `major` / `minor` so the backend can enrich names from `badge_assignments.csv`.

## Ubuntu VM Deployment

For a simple lab deployment, run both Mosquitto and this backend on the same Ubuntu VM.

### One-shot menu script

If you copied the full repo to the VM, you can use the setup helper at the repo root:

```bash
cd /path/to/Faculty_Locator
./vm_setup.sh
```

Menu options:

1. `Setup Mosquitto`
2. `Setup Dashboard`
3. `Uninstall Dashboard`
4. `Exit`

`Setup Dashboard` creates a Python virtual environment in the repo and installs a `systemd` service named `faculty-dashboard.service`.

### 1. Install and start Mosquitto

```bash
sudo apt-get update
sudo apt-get install -y mosquitto mosquitto-clients ufw

sudo tee /etc/mosquitto/conf.d/faculty-locator.conf >/dev/null <<'EOF'
listener 1883
allow_anonymous true
EOF

sudo systemctl enable mosquitto
sudo systemctl restart mosquitto
sudo ufw allow 1883/tcp
```

The minimal config is intentional. It avoids the service-start failures that can happen with a more complex first-pass Mosquitto config on Ubuntu.

### 2. Run the backend on the same VM

```bash
python3 -m venv .venv
source .venv/bin/activate
python3 -m pip install --upgrade pip
python3 -m pip install -r backend/requirements.txt
uvicorn backend.server:app --host 0.0.0.0 --port 8000
```

Then open:

`http://YOUR_VM_IP:8000`

### 3. Point the scanner to the VM

In `room_scanner/room_scanner.ino`, set:

- `MQTT_BROKER` to the Ubuntu VM's LAN IP
- `MQTT_PORT` to `1883`

The backend should keep using `localhost` for MQTT because it runs on the same VM as Mosquitto.

### 4. Quick broker test

On the VM:

```bash
mosquitto_sub -h localhost -t test/topic -v
```

In another terminal:

```bash
mosquitto_pub -h localhost -t test/topic -m hello
```

If you see `test/topic hello`, the broker is working.
