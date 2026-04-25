# Elite Energy — Firmware

ESP32-C3 firmware for the **Elite Energy WiFi dongle** that bridges
**Deye SUN-12K-SG04LP3** (3-phase LV battery) Modbus RTU traffic to
**three** independent ingestion endpoints in parallel.

> **Heritage**: register map borrowed from
> [Lewa-Reka/esphome-deye-inverter](https://github.com/Lewa-Reka/esphome-deye-inverter)
> (Apache 2.0). See `NOTICE` for attribution. Our additions live in
> `firmware/elite-base.yaml` and `firmware/components/elite_*` so a
> rebase from upstream stays mechanical.

---

## Architecture

```
                ┌──────────────────────────────────────────┐
                │           ESP32-C3-DevKitM-1             │
                │                                          │
                │   ESPHome runtime                        │
                │   ├── modbus_controller (RS485 RTU)      │
                │   │      polls Deye every 5 s            │
                │   │                                      │
                │   ├── stock mqtt:                        │
                │   │      → mqtts://elite.prygoda.xyz     │
                │   │                                      │
                │   ├── elite_partner_mqtt (custom)        │
                │   │      → mqtts://95.60.174.157:8883    │
                │   │                                      │
                │   └── elite_solarman_v5 (custom)         │
                │          → tcp://iot.talent-monitoring   │
                │                  .com:10000              │
                └────────┬─────────────────────────────────┘
                         │ Modbus RTU @ 9600 baud
                         ▼
                ┌────────────────┐
                │  Deye SUN-12K  │
                │   SG04LP3      │
                └────────────────┘
```

| # | Endpoint                          | Protocol             | Auth                  | Purpose                        |
|---|-----------------------------------|----------------------|-----------------------|--------------------------------|
| 1 | `elite.prygoda.xyz:8883`          | MQTT-TLS             | per-device user/pass  | dashboard + VPP dispatch       |
| 2 | `95.60.174.157:8883`              | MQTT-TLS             | anonymous             | partner ingestion              |
| 3 | `iot.talent-monitoring.com:10000` | Solarman V5 (binary) | logger SN handshake   | Solarman / Deye-app continuity |

Topic taxonomy on endpoints 1 + 2 is identical and matches the EMQX ACL
in [`samuildotnet/elite-energy/infra/emqx/acl.conf`](https://github.com/samuildotnet/elite-energy/blob/main/infra/emqx/acl.conf):

```
v1/t/{tenant}/s/{site}/i/{inverter}/sensor/{name}/state
v1/t/{tenant}/s/{site}/i/{inverter}/binary_sensor/{name}/state
v1/t/{tenant}/s/{site}/i/{inverter}/text_sensor/{name}/state
v1/t/{tenant}/s/{site}/i/{inverter}/status
v1/t/{tenant}/s/{site}/i/{inverter}/command/+   # incoming dispatch
```

---

## Hardware

ESP32-C3-DevKitM-1 + MAX3485 RS485 transceiver + LM2596 buck.
**Total BOM: ~$8.** See [`docs/bom.md`](docs/bom.md) for sourcing
links + Ukrainian distributor alternatives, [`docs/wiring.md`](docs/wiring.md)
for the schematic.

---

## Build & flash

### For end users (recommended)

Open **https://flash.elite.prygoda.xyz** in Chrome or Edge → click
"Connect" → select the USB device → click "Install". Done in 60 seconds
without any software install. Russian step-by-step:
[`docs/flash-instructions-ru.md`](docs/flash-instructions-ru.md).

### For developers

```bash
# 1. install ESPHome (one-time)
pipx install esphome

# 2. clone + populate secrets
git clone https://github.com/samuildotnet/elite-firmware
cd elite-firmware
cp secrets.example.yaml secrets.yaml
${EDITOR:-nano} secrets.yaml   # WiFi creds, per-device MQTT user/pw, Solarman SN

# 3. compile + flash over USB-C
esphome run firmware/devices/elite-deye-12k-01.yaml

# 4. (later) OTA push
esphome run firmware/devices/elite-deye-12k-01.yaml \
  --device elite-deye-12k-01.local
```

---

## Repo layout

```
firmware/
├── elite-base.yaml                 # Elite-Energy overrides (mqtt + ext components)
├── devices/
│   └── elite-deye-12k-01.yaml      # one file per physical inverter
└── components/
    ├── elite_partner_mqtt/         # 2nd MQTT-TLS publisher (anonymous)
    └── elite_solarman_v5/          # Solarman V5 binary push to cloud

docs/
├── wiring.md                       # ASCII pinout + RJ45 mapping
├── bom.md                          # parts list + AliExpress links
└── flash-instructions-ru.md        # Russian end-user quickstart

.github/workflows/
└── build.yml                       # ESPHome compile + artifact upload
```

A device YAML pulls the upstream Lewa-Reka `deye_hybrid_3p_lv` package,
overlays `elite-base.yaml`, and overrides the ESP32-C3 board + UART
pins. Every other thing — register definitions, sensor names, work
modes — lives upstream and refreshes on every build.

---

## Status of the three publishers

| Publisher              | Connect | Telemetry | Heartbeat | Notes |
|---|---|---|---|---|
| `mqtt:` (elite prod)   | ✅      | ✅        | ✅        | uses ESPHome stock MQTT |
| `elite_partner_mqtt`   | ✅      | ✅        | ✅        | hooks into App.get_sensors() — every state change is mirrored |
| `elite_solarman_v5`    | ✅      | ✅        | ✅        | parallel READ_HOLDING_REGISTERS reads → CRC-correct Modbus RTU response embedded in V5 frame |

The Solarman publisher mirrors a configurable list of `register_blocks`
(addresses + counts) to the cloud once per `push_interval`. Each block
becomes its own V5 data-report frame containing a complete Modbus RTU
function-0x03 response (`[slave][0x03][byte_count][data][crc16]`) — the
same wire format the stock LSW3 dongle uses, so the customer's Deye-app
keeps working unchanged.

Reads are queued through the same `modbus_controller` instance that
ESPHome uses for sensor polling (`ModbusController::queue_command`).
At 9600 baud RTU a full default-block sweep (170 registers) costs
< 200 ms of bus time per minute — comfortably under the inverter's
budget. Tweak `firmware/elite-base.yaml` → `elite_solarman_v5.register_blocks`
for non-default Deye families.

---

## License

Apache 2.0 — see `LICENSE` and `NOTICE` for upstream attribution.
