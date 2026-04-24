# Wiring — Elite Energy WiFi dongle ↔ Deye SUN-12K-SG04LP3

```
                                                  ┌────────────────────┐
                                                  │  ESP32-C3-DevKitM-1 │
                                                  │                    │
                              5V from buck ──── 5V│●                   │
                                       GND ───  G │●                   │
                                                  │                    │
                  ┌────────────┐                  │  GPIO20 (RX) ●─────┼── RO of MAX3485
                  │  MAX3485   │                  │  GPIO21 (TX) ●─────┼── DI of MAX3485
                  │  RS485     │                  │  GPIO5  (DE) ●─────┼── RE+DE of MAX3485
                  │  half-dpx  │                  │                    │
                  │            │                  └────────────────────┘
   Inverter A ────│A          R│O ────────────────── GPIO20  (RX)
                  │            │
   Inverter B ────│B          D│I ────────────────── GPIO21  (TX)
                  │            │
                  │   RE+DE    │
                  │     ●──────┼─────────────────── GPIO5   (DE/RE)
                  │            │
                  │   VCC  GND │
                  └────┬────┬──┘
                       │    │
                       5V   GND
                       (from same buck as ESP32)
```

## Pinout reference

| ESP32-C3 pin | Function          | Wire to                     |
|---|---|---|
| 5V           | Power input       | Buck output 5 V             |
| GND          | Ground            | Buck GND + MAX3485 GND      |
| GPIO20       | UART RX           | MAX3485 RO (Receive-Out)    |
| GPIO21       | UART TX           | MAX3485 DI (Driver-In)      |
| GPIO5        | RS485 TX-enable   | MAX3485 RE + DE (tied)      |

## Inverter side

The Deye SUN-12K-SG04LP3 exposes RS485 over its **BMS / RS485** RJ45
port on the bottom edge. **Do NOT use the BMS-only port** — it speaks
CAN, not Modbus RTU.

| RJ45 pin | Function    | Cable colour (T568B) |
|---|---|---|
| 1        | RS485 A+    | white-orange         |
| 2        | RS485 B-    | orange               |
| 7        | 12 V aux    | white-brown          |
| 8        | GND         | brown                |

Power the dongle from pins 7 + 8 through a **9–24 V → 5 V** LM2596 buck
module. Don't try to feed 12 V directly into the ESP32 5V rail — it
will cook.

## Modbus settings

| Param        | Value     |
|---|---|
| Baud rate    | 9600      |
| Data bits    | 8         |
| Parity       | None      |
| Stop bits    | 1         |
| Slave address| 0x01      |

These are the Deye factory defaults. If the inverter has been
re-addressed (e.g. on multi-inverter sites), change
`modbus_inverter_address` in the device YAML.
