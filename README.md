# Elite Energy — Firmware

ESP32-C3 firmware for the **Elite Energy WiFi dongle** that bridges
**Deye SUN-* SG03LP1 / SG04LP3** (LV battery hybrids) Modbus RTU traffic
to our prod broker (`mqtt.elite.prygoda.xyz`) and, optionally, to
Solarman cloud.

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
                │   ├── elite_provisioning (custom)        │
                │   │      reads NVS namespace             │
                │   │      ``elite-cfg`` at boot           │
                │   │                                      │
                │   ├── modbus_controller (RS485 RTU)      │
                │   │      polls Deye every 5 s            │
                │   │                                      │
                │   ├── stock mqtt:                        │
                │   │      → mqtts://mqtt.elite            │
                │   │           .prygoda.xyz:8883          │
                │   │                                      │
                │   └── elite_solarman_v5 (custom, opt-in) │
                │          → tcp://iot.talent-monitoring   │
                │                  .com:10000              │
                │      gated on elite-cfg.solarman_sn      │
                └────────┬─────────────────────────────────┘
                         │ Modbus RTU @ 9600 baud
                         ▼
                ┌────────────────┐
                │  Deye SUN-*    │
                │   LV hybrid    │
                └────────────────┘
```

| # | Endpoint                          | Protocol             | Auth                            | Purpose                        |
|---|-----------------------------------|----------------------|---------------------------------|--------------------------------|
| 1 | `mqtt.elite.prygoda.xyz:8883`     | MQTT-TLS             | per-device (NVS device_id/token)| dashboard + VPP dispatch       |
| 2 | `iot.talent-monitoring.com:10000` | Solarman V5 (binary) | logger SN handshake (NVS)       | Solarman / Deye-app continuity |

The partner publisher that lived alongside endpoint #1 in v0.1.0 was
removed in v0.2.0 — the dongle now publishes only to our prod broker
plus, optionally, Solarman cloud. The optional Solarman publisher is
gated on the ``elite-cfg.solarman_sn`` NVS key: leaving it empty at
provision time disables the publisher entirely (no second TCP socket,
no log noise, no FreeRTOS connect task).

Topic taxonomy on endpoint #1 matches the EMQX ACL in
[`samuildotnet/elite-energy/infra/emqx/acl.conf`](https://github.com/samuildotnet/elite-energy/blob/main/infra/emqx/acl.conf):

```
v1/t/{tenant}/s/{site}/i/{inverter}/sensor/{name}/state
v1/t/{tenant}/s/{site}/i/{inverter}/binary_sensor/{name}/state
v1/t/{tenant}/s/{site}/i/{inverter}/text_sensor/{name}/state
v1/t/{tenant}/s/{site}/i/{inverter}/status
v1/t/{tenant}/s/{site}/i/{inverter}/command/+   # incoming dispatch
```

---

## NVS-driven provisioning (v0.2.0)

The dongle reads a 24 KB NVS partition (default ``nvs`` @ ``0x9000``)
written by the elite-energy backend at flash time. Namespace:
``elite-cfg``.

| Key              | Type | Required | Default                       | Notes                                              |
|------------------|------|----------|-------------------------------|----------------------------------------------------|
| `inverter_model` | str  | yes      | —                             | slug, e.g. ``deye-12k-sg04lp3``                    |
| `device_id`      | str  | yes      | —                             | UUIDv4 from backend                                |
| `device_token`   | str  | yes      | —                             | MQTT password (24-char URL-safe)                   |
| `mqtt_host`      | str  | no       | ``mqtt.elite.prygoda.xyz``    | TLS broker on 8883                                 |
| `wifi_ssid`      | str  | no       | (empty)                       | home WiFi SSID                                     |
| `wifi_pw`        | str  | no       | (empty)                       | home WiFi password                                 |
| `solarman_sn`    | str  | no       | (empty)                       | 10-digit LSW3 SN; empty disables Solarman cloud    |

The schema is parsed by `elite_provisioning` (see
`firmware/components/elite_provisioning/`). Host-side unit tests for
the parser live in `firmware/components/elite_provisioning/test/` and
run without the ESP-IDF toolchain (`make`).

> **v0.2.0 backward compatibility:** existing v0.1.0 dongles in the
> field have an empty NVS partition. After flashing v0.2.0 they will
> log a fatal-style error (``elite-cfg namespace missing
> device_id/device_token``) and require re-provisioning through the
> web flasher. This is intentional — v0.2.0 is incompatible with
> in-the-field v0.1.0 swistles without re-provisioning.

---

## Hardware

ESP32-C3-DevKitM-1 + MAX3485 RS485 transceiver + LM2596 buck.
**Total BOM: ~$8.** See [`docs/bom.md`](docs/bom.md) for sourcing
links + Ukrainian distributor alternatives, [`docs/wiring.md`](docs/wiring.md)
for the schematic.

---

## Build & flash

### For end users (recommended)

Open **https://elite.prygoda.xyz/flash/** in Chrome or Edge → pick the
inverter model, optionally enter WiFi creds + Solarman SN → click
**INSTALL**. The page generates a per-device NVS partition on the
backend and ships it to the browser flasher together with the firmware
binary; ESP-Web-Tools writes both in one operation. Done in 60 s
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
${EDITOR:-nano} secrets.yaml   # WiFi creds, per-device MQTT user/pw

# 3. compile + flash over USB-C
esphome run firmware/devices/elite-deye-12k-01.yaml

# 4. (later) OTA push
esphome run firmware/devices/elite-deye-12k-01.yaml \
  --device elite-deye-12k-01.local
```

To exercise the elite_provisioning parser without flashing:

```bash
make -C firmware/components/elite_provisioning/test
```

---

## Repo layout

```
firmware/
├── elite-base.yaml                 # Elite-Energy overrides
├── devices/
│   └── elite-deye-12k-01.yaml      # one file per physical inverter
└── components/
    ├── elite_provisioning/         # NVS namespace reader
    │   └── test/                   # host-side unit tests
    └── elite_solarman_v5/          # Solarman V5 binary push to cloud

docs/
├── wiring.md                       # ASCII pinout + RJ45 mapping
├── bom.md                          # parts list + AliExpress links
└── flash-instructions-ru.md        # Russian end-user quickstart

.github/workflows/
├── build.yml                       # ESPHome compile + artifact upload
└── host-tests.yml                  # elite_provisioning unit tests
```

A device YAML pulls the upstream Lewa-Reka `deye_hybrid_3p_lv` package,
overlays `elite-base.yaml`, and overrides the ESP32-C3 board + UART
pins. Every other thing — register definitions, sensor names, work
modes — lives upstream and refreshes on every build.

---

## Status of the publishers (v0.2.0)

| Publisher              | Connect | Telemetry | Heartbeat | Notes |
|---|---|---|---|---|
| `mqtt:` (elite prod)   | ✅      | ✅        | ✅        | stock ESPHome MQTT, TLS |
| `elite_solarman_v5`    | ✅      | ✅        | ✅        | gated on `elite-cfg.solarman_sn`; parallel READ_HOLDING_REGISTERS reads → CRC-correct Modbus RTU response embedded in V5 frame |

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
