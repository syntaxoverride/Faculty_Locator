# Lab: Build a BLE Faculty Presence Badge

**Course:** IoT Security / Cyber Defense Fundamentals  
**Duration:** 2–3 lab sessions  
**Difficulty:** Intermediate  

## Objective

Build a wearable BLE badge that broadcasts an iBeacon signal, detectable by an ESP32 room scanner. The result is a simplified, classroom-scale version of the Real-Time Location Systems (RTLS) used in large organizations and enterprise environments.

By the end of this lab, you will understand how BLE advertising works, what iBeacon protocol frames look like, how presence detection differs from location tracking, and the privacy/security implications of wireless tracking systems.

## Background

### How Enterprise RTLS Works

Enterprise RTLS platforms often use Ultra-Wideband (UWB) radio with ceiling-mounted anchors to achieve sub-meter accuracy. People wear rechargeable badges that transmit signals to multiple receivers, which use time-difference-of-arrival (TDoA) calculations to triangulate exact position. These systems can cost tens of thousands of dollars per floor.

### What We're Building (Classroom Presence Detection)

We don't need to know *where* in the room someone is — just whether they're *in the room at all*. Presence detection is a binary problem (present/absent), which is dramatically simpler:

- **Enterprise RTLS:** Multiple receivers per room, UWB, triangulation math, sub-meter precision, $$$
- **Our system:** One receiver per room, BLE, signal strength threshold, room-level precision, ~$8 per badge

### The iBeacon Protocol

Apple's iBeacon (2013) is a BLE advertising format. A beacon broadcasts a fixed packet at a regular interval. The packet contains:

| Field | Size | Purpose |
|-------|------|---------|
| UUID | 16 bytes | Organization identifier (same for all our badges) |
| Major | 2 bytes | Group (1=faculty, 2=staff, etc.) |
| Minor | 2 bytes | Individual ID (1=Dr. Smith, 2=Dr. Jones) |
| TX Power | 1 byte | Calibrated signal strength at 1 meter |

The receiver doesn't connect to the badge — it just passively listens for these advertisements. That matters because BLE advertising is one-way, connectionless, and low-power.

## Bill of Materials (Per Badge)

| Component | Purpose | Approximate Cost |
|-----------|---------|-----------------|
| ESP32-C6 Mini | Microcontroller with BLE | $2–5 |
| TP4056 USB-C charge module | Battery charging with protection | $0.50–1 |
| 3.7V LiPo battery (400–600mAh) | Power source | $3–5 |
| Slide switch (SPDT) | On/off | $0.10 |
| 3D-printed enclosure (optional) | Badge housing | ~$1 in filament |
| Lanyard clip or badge reel | Wearability | $0.50–1 |
| **Total** | | **~$7–12** |

### Recommended Exact Parts

- **ESP32-C6 Mini** — Search "ESP32-C6 Mini" on Amazon or AliExpress. Get a USB-C version if possible. For this project, a C6 mini is a good wearable badge board because it supports BLE while staying small and inexpensive.
- **TP4056 module** — Get the version with DW01A battery protection IC (has two ICs on board, not one). The protection circuit prevents over-discharge, which will kill a LiPo.
- **Battery** — A 502035 or 602535 LiPo fits well. 400–600mAh is the sweet spot for badge size vs. battery life.

## Part 1: Wiring

### Circuit Diagram

```
                    TP4056 Module
                 ┌─────────────────┐
  USB-C ────────►│ IN+         B+  │──────► ESP32-C6 "5V" pin
  (charging)     │ IN-         B-  │──────► ESP32-C6 "GND" pin
                 └──────┬──────┬───┘
                        │      │
                   BAT+ │      │ BAT-
                        │      │
                 ┌──────┴──────┴───┐
                 │   3.7V LiPo     │
                 │   400-600mAh    │
                 └─────────────────┘


  Optional: Add a slide switch between TP4056 B+ and ESP32 5V pin
  so faculty can turn the badge off.
```

### Wiring Steps

1. **DO NOT** connect the battery yet.
2. Solder two wires from the TP4056 output pads (B+ and B-) to the ESP32-C6 mini. B+ goes to the board's `5V` or `VBUS/5V` input pin if your specific board exposes one. B- goes to GND. Check the exact pin labels on your board before soldering because C6 mini variants are not always identical.
3. Solder the LiPo battery wires to the TP4056 BAT+ and BAT- pads. **Triple-check polarity.** Reversed polarity on a LiPo can cause fire.
4. Optionally, add a slide switch in-line with the B+ wire to ESP32.

### Safety Notes

- LiPo batteries can be dangerous if shorted, punctured, or overcharged. Handle with care.
- The TP4056 with DW01A protection IC prevents over-charge, over-discharge, and short circuit. Always use the protected version.
- If a LiPo puffs up (swells), stop using it immediately. Dispose of it properly.

## Part 2: Firmware

### Arduino IDE Setup

1. Open Arduino IDE. Go to **File → Preferences**.
2. In "Additional Board Manager URLs", add:  
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
3. Go to **Tools → Board → Boards Manager**, search "esp32", install **esp32 by Espressif Systems**.
4. Select board: **Tools → Board → ESP32C6 Dev Module**
5. If your specific board supports it, set **USB CDC On Boot: Enabled** so Serial Monitor works reliably over USB.

### Flash the Badge Firmware

1. Open `badge_beacon.ino` from the project files.
2. **Change `BEACON_MINOR` to your assigned badge number.** Each student/faculty gets a unique minor value. The minor value is the only thing that differs per badge.
3. Connect the ESP32-C6 mini via USB.
4. If the board doesn't appear as a COM port: hold the BOOT button, press RESET, release RESET, then release BOOT. That sequence forces download mode.
5. Click Upload.
6. Open Serial Monitor (115200 baud) — you won't see much because the badge immediately goes to deep sleep. But you'll see a brief initialization message on each wake cycle if you add `Serial.begin(115200); Serial.println("Wake!");` before the BLE init.

### Understanding the Firmware

Read through `badge_beacon.ino` and answer these questions:

**Q1:** Why does the firmware use `esp_deep_sleep()` instead of `delay()`?

*Deep sleep drops power consumption from ~130mA to ~5µA. With `delay()`, the CPU stays fully active and drains the battery in hours instead of weeks.*

**Q2:** Why does the code call `BLEDevice::deinit(true)` before sleeping?

*It cleanly shuts down the BLE stack and releases its controller memory before deep sleep. That reduces sleep current and is safe here because waking from deep sleep restarts the badge from `setup()` anyway.*

**Q3:** The badge advertises for only 150ms, then sleeps for 1 second. Why is this enough for the room scanner to detect it?

*The room scanner runs continuous 5-second scan windows. Even if the badge's 150ms advertisement doesn't align perfectly with the scan start, within 5 seconds there will be multiple wake cycles (at least 3-4), making detection highly reliable.*

**Q4:** What would happen if two badges had the same Major AND Minor values?

*The room scanner would see them as the same person. It would show one faculty member as present when either badge is in range, but couldn't distinguish which person is actually there.*

## Part 3: Verification

### Using nRF Connect (Phone App)

1. Install **nRF Connect** (free, by Nordic Semiconductor) on your phone.
2. Open the app, tap **Scan**.
3. Look for an iBeacon advertisement from your badge. Depending on the phone and app view, the device name `BADGE-1-X` may not appear in the main scan list because the iBeacon payload uses almost the entire advertisement packet.
4. Tap on it. You should see:
   - **iBeacon** advertisement type
   - Your UUID, Major, and Minor values
   - RSSI (signal strength) — this will change as you move your phone closer/farther

### Using the Room Scanner

If the room scanner ESP32 is already running:

1. Power on your badge (connect battery or plug in USB).
2. Watch the scanner's Serial Monitor output.
3. You should see: `[DETECTED] Badge-1-X (RSSI: -XX dBm)`
4. Walk out of the room. After the timeout period (default 120 seconds), you should see the scanner publish an "absent" message.

### Measuring Battery Life

To estimate battery life, you need two numbers:

- **Average current** = (active current × active time + sleep current × sleep time) / total cycle time
- **Battery capacity** in mAh

With a 1-second cycle:
```
Active:  130mA × 0.15s  = 19.5 mA·s
Sleep:     0.005mA × 0.85s = 0.00425 mA·s
Average: 19.504 mA·s / 1s = 19.5 mA average

With 500mAh battery: 500 / 19.5 = ~25.6 hours
```

Wait — that's not great. Here's where optimization matters:

**With 2-second cycle:**
```
Active:  130mA × 0.15s  = 19.5 mA·s
Sleep:     0.005mA × 1.85s = 0.00925 mA·s  
Average: 19.509 / 2 = ~9.75 mA

With 500mAh battery: 500 / 9.75 = ~51 hours ≈ 2 days
```

**With 5-second cycle:**
```
Average: 19.5 / 5 = ~3.9 mA
With 500mAh battery: 500 / 3.9 = ~128 hours ≈ 5 days
```

**Reality check:** These are rough estimates. Actual deep sleep current on ESP32-C6 mini boards varies by board variant, regulator choice, and USB-serial implementation. Measure your actual board with a multimeter in series instead of assuming the published deep sleep number is what you'll get in practice.

**Key insight for students:** This is why commercial BLE beacons (which get 1+ year of battery life) use dedicated BLE SoCs like the Nordic nRF52 instead of general-purpose microcontrollers. The nRF52's deep sleep is under 2µA and its BLE stack is far more power-optimized. The ESP32 is great for prototyping but isn't the chip you'd choose for a production badge.

## Part 4: Enclosure

### Option A: 3D Printed Badge

If your lab has a 3D printer, a simple two-piece snap-fit enclosure works well. Target dimensions for an ESP32-C6 mini + TP4056 + small LiPo:

- Outer: ~70mm × 45mm × 15mm (about the size of an ID card but thicker)
- Add a slot for a lanyard loop or badge clip
- Include a small hole for the USB-C port (charging access)
- Optional: small window for the status LED

Search Thingiverse or Printables for "ESP32 C6 case" or "ESP32 badge case" for starting designs to modify.

### Option B: Altoids Tin / Off-the-Shelf

A small mint tin or project box from the electronics shop works fine for prototyping. Not as polished, but functional. Drill a hole for USB-C access.

### Option C: Badge Holder Hack

Get a standard vertical badge holder (the clear plastic kind). An ESP32-C6 mini + thin LiPo can fit behind a printed paper ID card insert, depending on the exact board dimensions. For a demo, that is often the most professional-looking option.

## Part 5: Security Analysis

The next section focuses on cybersecurity. Answer the following:

**Q5: Spoofing.** The iBeacon protocol has no authentication. What stops an attacker from cloning a faculty badge's UUID/Major/Minor and appearing as that person?

*Nothing in the protocol itself. iBeacon advertisements are unencrypted and unauthenticated. Anyone with an ESP32 and the UUID/Major/Minor values can impersonate a badge. Mitigations include: rotating UUIDs (like Apple's Find My), using encrypted BLE advertisements (requires custom protocol), or adding a secondary verification factor.*

**Q6: Tracking.** If someone knows the UUID being broadcast, they could deploy their own receivers to track faculty movement across campus. How does this compare to the privacy implications of carrying a phone?

*A phone broadcasts BLE, WiFi probe requests, and cellular signals — far more trackable than a purpose-built badge. However, modern phones use MAC address randomization. Our badges broadcast a fixed, predictable identifier, making them easier to track. Commercial systems mitigate this with rotating identifiers.*

**Q7: Denial of Service.** How could an attacker prevent the presence system from working?

*BLE jamming at 2.4GHz (illegal, but trivial with SDR hardware). Flooding the scanner with thousands of spoofed advertisements to overwhelm its processing. Physically blocking the badge signal with a Faraday pouch. Draining the badge battery by preventing deep sleep (harder to do remotely).*

**Q8: Zero Trust Implications.** If you were designing this system under Zero Trust principles, what would you change?

*Never trust the badge identity at face value — require mutual authentication. Encrypt advertisements so only authorized scanners can decode them. Log all presence events immutably for audit. Implement anomaly detection (e.g., same badge seen in two rooms simultaneously). Use short-lived, rotating credentials instead of static UUIDs. Verify at every layer — don't assume the MQTT channel or backend is secure either.*

## Deliverables

1. Working BLE badge that broadcasts iBeacon advertisements (demonstrate with nRF Connect)
2. Verified detection by the room scanner (show Serial Monitor output)
3. Written answers to questions Q1–Q8
4. (Bonus) Measured actual deep sleep current with a multimeter and calculated realistic battery life
5. (Bonus) 3D-printed or custom enclosure

## Reference: Complete System Architecture

```mermaid
flowchart LR
    badge["Faculty Badge<br/>ESP32-C6 Mini + LiPo<br/><br/>Broadcasts UUID + Major + Minor every 1-2s<br/>~$8 per badge"]
    scanner["Room Scanner<br/>ESP32-WROOM (USB powered)<br/><br/>Scans for known badge UUIDs<br/>Checks RSSI threshold<br/>~$8 per room"]

    subgraph backend["Backend"]
        broker["FastAPI + Mosquitto"]
        dashboard["Web Dashboard"]
    end

    badge -- "BLE broadcast (iBeacon)" --> scanner
    scanner -- 'WiFi / MQTT: "dr_smith: IN"' --> broker
    broker --> dashboard
```

For a simple demo deployment, run both Mosquitto and the dashboard backend on the same Ubuntu VM. The scanner ESP32 should publish to the VM's LAN IP, while the backend can connect to the broker on `localhost:1883`.
