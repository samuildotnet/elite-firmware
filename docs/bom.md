# Bill of Materials — Elite Energy WiFi dongle

Total per unit: **~$8** (AliExpress prices, single quantity).

| # | Part                                  | Qty | Price | Source / part number |
|---|---------------------------------------|-----|-------|----------------------|
| 1 | ESP32-C3-DevKitM-1 (USB-C, 4 MB)      | 1   | $4.00 | AliExpress, search "ESP32-C3-DevKitM-1 USB-C" |
| 2 | MAX3485ESA RS485 transceiver (SOIC-8) | 1   | $0.50 | LCSC C5184, AliExpress |
| 3 | LM2596 buck DC-DC (9–24 V → 5 V, 1 A) | 1   | $1.00 | AliExpress, search "LM2596 module" |
| 4 | RJ45 module + 1 m shielded cat5e      | 1   | $1.50 | Any supplier |
| 5 | 3D-printed enclosure (PETG)           | 1   | $0.50 | Internal (`hardware/enclosure.stl`) |
| 6 | 2.54 mm pin headers + screws          | —   | $0.50 | Generic |
|   | **Total**                             |     | **$7.50** |  |

## Recommended supplier links

- ESP32-C3-DevKitM-1 — order direct from Espressif's
  [aliexpress store](https://www.aliexpress.com/item/1005005595291796.html)
  for guaranteed authentic boards.
- MAX3485 — `Maxim MAX3485ESA+T` from Mouser if you need 100 % pin-to-pin,
  or `SP3485EN` from any AliExpress reseller (drop-in replacement).
- LM2596 — search "LM2596 buck module 9-24V" — every shop sells the
  same Chinese clone, $0.80–$1.20.

## In-Ukraine substitutions

| Stock part   | Local equivalent                          |
|---|---|
| ESP32-C3     | radioteh.ua, sku ESP32C3-DevKitM-1, ≈220₴ |
| MAX3485      | radioteh.ua, sku MAX3485ESA, ≈30₴         |
| LM2596       | every "DIY" shop on Rozetka, ≈80₴         |
| RJ45 cable   | any electronics retailer, ≈50₴            |
|              | **Local total**: **≈400 ₴**               |

## Tooling (one-time per assembler)

| Item                              | Approx. price |
|---|---|
| USB-C to USB-A cable (data)       | $2            |
| Soldering iron + flux + wire      | $20 (one-time)|
| 3D printer access (PETG)          | $0 if you have a friend |
