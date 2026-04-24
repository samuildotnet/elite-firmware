# Elite Energy — Firmware

Firmware for the Elite Energy WiFi / Cellular dongle that bridges
inverter Modbus RTU to our cloud MQTT broker over TLS.

> **Heritage**: forked from
> [lewa-reka/esphome-deye-inverter](https://github.com/lewa-reka/esphome-deye-inverter)
> (Apache 2.0). See `NOTICE` for attribution. Our additions are kept
> in `firmware/elite-*.yaml` and `firmware/profiles/elite-*.yaml` so a
> rebase from upstream stays mechanical.

---

## What this firmware does

```
                 ┌───────────────────────────────────────┐
                 │  ESP32-C3 (Wi-Fi)  /  ESP32-S3+SIM7080 │
                 │                                       │
                 │   ESPHome runtime                     │
                 │   ├── modbus_controller (RS485 RTU)   │
                 │   │     polls Deye / Huawei / …       │
                 │   ├── mqtt (TLS, per-device user/pw)  │
                 │   ├── ota (signed)                    │
                 │   └── captive_portal (provisioning)   │
                 └───────┬───────────────────┬───────────┘
                         │                   │
                  RS485 / Modbus RTU     mqtts://elite.prygoda.xyz:18883
                         │                   │
                  ┌──────┴──────┐      ┌─────┴───────┐
                  │  Inverter   │      │   EMQX      │
                  │  (Deye …)   │      │   broker    │
                  └─────────────┘      └─────────────┘
```

Telemetry is published to
`v1/t/{tenant}/s/{site}/i/{inverter}/sensor/{name}/state`.
Dispatch commands are received on
`v1/t/{tenant}/s/{site}/i/{inverter}/command/+`. The topic taxonomy
matches the EMQX ACL rules in
[`samuildotnet/elite-energy/infra/emqx/acl.conf`](https://github.com/samuildotnet/elite-energy/blob/main/infra/emqx/acl.conf).

---

## Hardware

### Track 1 — Wi-Fi dongle (MVP, this repo)

| Part | Model | Qty | Approx. price |
|---|---|---:|---:|
| MCU | ESP32-C3-DevKitM-1 | 1 | $4 |
| RS485 transceiver | MAX3485 / SP3485 (SOIC-8) | 1 | $0.50 |
| Buck DC-DC | LM2596 module (9–24 V → 5 V, 1 A) | 1 | $1 |
| Cable | 4-wire shielded, 1 m, RJ45 or Phoenix | 1 | $1.50 |
| Enclosure | 3D-printed PETG case | 1 | $0.50 |
| Misc | Pin headers, screws, heat-shrink | — | $0.50 |
| **Total BOM** | | | **~$8** |

### Track 2 — Cellular dongle (post-MVP, deferred — see ADR-002)

ESP32-S3-WROOM-1 + SIM7080G. Will live on the same firmware base, only
the board YAML changes.

---

## Wiring

```
       Inverter RS485 port
       ┌─────────────────┐
       │ A+ (D+)  B- (D-)│
       └──┬───────────┬──┘
          │           │
          │           │
   ┌──────┴───────────┴──────┐
   │  MAX3485 RS485 transceiver│
   │  RO  RE  DE  DI    A  B   │
   └───┬────┬───┬───┬────────────┘
       │    │   │   │
       │    └───┘   │       (RE/DE tied together,
       │      │     │        driven by GPIO5 = TX_EN)
       │      │     │
   GPIO20  GPIO5  GPIO21      ESP32-C3-DevKitM-1
    (RX)   (TX_EN) (TX)
```

Power: feed 9–24 V from inverter's auxiliary RJ45 pins through the
LM2596 buck → 5 V → ESP32 5V pin. Common ground.

See `docs/wiring.md` for full pinout + photos once first board is
assembled.

---

## Build & flash

```sh
# 1. install ESPHome (one-time)
pipx install esphome

# 2. clone + populate secrets
git clone https://github.com/samuildotnet/elite-firmware
cd elite-firmware
cp secrets.example.yaml secrets.yaml
${EDITOR:-nano} secrets.yaml   # fill in WiFi creds, MQTT user/pw

# 3. compile + flash over USB-C
esphome run firmware/devices/elite-deye-12k-01.yaml

# 4. (later) OTA push
esphome run firmware/devices/elite-deye-12k-01.yaml \
  --device elite-deye-12k-01.local
```

The device will:
1. Connect to Wi-Fi using `secrets.yaml`. If it fails, falls back to
   `Elite-Setup-XXXX` SoftAP for 5 min — phone connects, opens
   `192.168.4.1`, enters real Wi-Fi creds. Saved to NVS.
2. Connect to `mqtts://elite.prygoda.xyz:18883` (TLS, server cert
   pinned to our CA, client auth via username/password from
   `secrets.yaml`).
3. Start polling Modbus regs every 5 s and publishing to
   `v1/t/.../sensor/.../state`.
4. Subscribe to `v1/t/.../command/+` for dispatch.

---

## Repo layout

```
firmware/
├── elite-base.yaml          # shared: WiFi, MQTT-TLS, OTA, captive portal
├── profiles/
│   ├── deye-sg0xlp1.yaml    # SUN-{5,8,10,12}K-SG04LP1  (1-phase LV)
│   ├── deye-sg0xlp3.yaml    # SUN-{5,8,10,12}K-SG04LP3  (3-phase LV) ← MVP
│   └── deye-sg0xhp3.yaml    # SUN-{30,50,100}K-SG01HP3  (3-phase HV)
└── devices/
    └── elite-deye-12k-01.yaml   # one file per physical inverter

boards/
└── elite-wifi-c3.yaml       # ESP32-C3 + MAX3485 board definition

docs/
├── wiring.md
└── bom.md
```

A device YAML is just substitutions + `<<: !include` of base + profile.
To add a new inverter: copy `elite-deye-12k-01.yaml`, change the
substitutions (tenant slug, site slug, inverter slug, MQTT creds),
flash.

---

## License

Apache 2.0 — inherited from the upstream
[lewa-reka/esphome-deye-inverter](https://github.com/lewa-reka/esphome-deye-inverter).
See `LICENSE` and `NOTICE`.
