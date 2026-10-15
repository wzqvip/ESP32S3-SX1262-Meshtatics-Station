# ESP32-S3 SX1262 Meshtatics Station

A modular ESP32-S3 LoRa station designed around the T-Beam-S3 pinout.
The board keeps the main peripheral assignments close to the T-Beam-S3 family,
so firmware written for a compatible T-Beam-S3 target can be adapted with
minimal hardware-specific changes.

The design goal is simple assembly and easy customization: the functional
blocks are built from plug-in or module-style parts instead of requiring a
complex hand-soldered RF and sensor circuit.

> Hardware revision: 2026-09-06
>
> The schematic and netlist exports in `PCB/` carry a 2026-10-14 date. Their
> content is identical to the 2026-09-06 revision; only the export date differs.
>
> This repository contains the current design files. Check the schematic and
> the selected module datasheets before ordering or powering a board.

## Highlights

- Waveshare ESP32-S3-Pico main controller with an **ESP32-S3 N8R2** module
  (8 MB flash, 2 MB PSRAM), or a compatible version
- SX1262-based Ebyte **E22-900M33S** 2 W LoRa radio (see the power note in
  *LoRa radio* below — this module must **not** be driven like a 900M30S)
- Hardware TX/RX switching: one SX1262 DIO2 signal drives the E22 `TXEN` through a
  solder bridge and `RXEN` through a Schmitt-trigger inverter, so no MCU GPIO is
  needed for RF switching
- Socketed GPS, motion, pressure, RTC, and OLED modules
- T-Beam-S3-oriented GPIO assignments
- 12 V input through either the DC barrel jack or the USB-C PD decoy board
- Modular construction for easier assembly and replacement
- Manufacturing outputs included for PCB fabrication and assembly

## Hardware Modules

### Controller

The board uses the [Waveshare ESP32-S3-Pico](https://www.waveshare.com/wiki/ESP32-S3-Pico)
as its processing core. A compatible ESP32-S3-Pico version can also be used if
its mechanical dimensions, pinout, voltage levels, flash/PSRAM configuration,
and USB implementation match the board requirements. It provides
Wi-Fi/Bluetooth-capable ESP32-S3 processing, USB connectivity through the
module design, and the GPIO resources used by the radio and sensors.

The Pico is powered from the board's 5 V rail through its `VBUS` pins. Its
onboard 3.3 V regulator output (`3V3 OUT`) is used on this board for only one
load, the radio control inverter.

### LoRa radio

The radio is the [Ebyte E22-900M33S](https://www.cdebyte.com/products/E22-900M33S),
an SX1262-compatible LoRa module for the US 900 MHz band with an integrated PA
and LNA. It is rated for up to 2 W (33 dBm) transmit power. The module is
connected through SPI, with separate signals for reset, busy, and DIO1.

> ### ⚠️ Critical: the RF chip input must not exceed 9 dBm
>
> The E22-900M33S datasheet states plainly:
> **"The output power of the RF chip cannot be greater than 9 dBm."**
>
> This module's PA has roughly **25 dB** of gain. If firmware drives the SX1262 to
> its own maximum (+22 dBm) — which is correct for an E22-900M30S — the PA input
> is overdriven by about **13 dB** and the module can be damaged.
>
> Do **not** assume an E22-900M30S configuration works here. The two modules are
> pin-compatible and look identical, but their power tables are nearly opposite:
>
> | Module | Rated output | RF chip input limit | PA gain | TX current |
> | --- | --- | --- | --- | --- |
> | E22-900M30S | 30.0 dBm (1 W) | datasheet sets **no** limit | ~7 dB | 650 mA |
> | **E22-900M33S** | **33 dBm (2 W)** | **≤ 9 dBm** | **~25 dB** | **1200 mA** |
>
> In Meshtastic this is selected with the build define `EBYTE_E22_900M33S`
> (which yields `TX_GAIN_LORA 25` / `SX126X_MAX_POWER 8`). The variant in
> `Review/meshtastic-variant/` uses it, and a compile-time assertion fails the
> build if the wrong module define is selected.

The E22 module also has dedicated `TXEN` and `RXEN` inputs for its internal RF
switch. On this board those are driven from a single SX1262 pin:

```
E22 DIO2 ──┬── solder bridge U4 ────────────────> E22 TXEN
           └── SN74LVC1G14 inverter (U3) ──────> E22 RXEN
```

`U4` is shorted by default. With it in place, DIO2 high selects the transmit
path and DIO2 low selects the receive path, so the RF switch follows the radio
automatically. Because the inversion is done in hardware, the MCU does not
control `TXEN`/`RXEN`, and no GPIO is spent on RF switching.

> **Firmware requirement:** the SX1262 must be told to drive DIO2 as its RF
> switch control. With RadioLib this is `radio.setDio2AsRfSwitch(true)`. If that
> step is missing, DIO2 is not driven, the E22 stays in software shutdown, and
> the board appears to be unable to transmit or receive at all.

The `SN74LVC1G14` inverter is powered from the Pico's `3V3 OUT` rail, matching
the SX1262's 3.3 V logic levels.

The PCB footprint accepts other modules from the E22 family, such as the
E22-400M22S, E22-900M22S, and E22-900M30S, provided the module dimensions,
pinout, supply requirements, RF band, and control behavior match. This is a
hardware-level compatibility target, not a guarantee that any E22 variant will
work without firmware or RF configuration changes — **and each variant has its
own RF-chip input limit, so the power table must always be matched to the module
actually fitted.**

The E22 module uses 3.3 V logic and accepts a 2.5-5.5 V supply; this board
supplies it from 5 V, which the 33S requires for guaranteed output power
(≥ 5.0 V). A 2 W transmit mode draws about **1200 mA** instantaneous current, so
use a stable supply, a suitable antenna, and an appropriate enclosure. 2 W also
requires a radio licence or other authorization in most regions — verify local
regulations before enabling full power.

A 2 W transmit burst on the 33S draws roughly **1200 mA** (±100 mA @ 33 dBm, 5 V),
which is the number to design the supply and wiring around. The board provides a
470 uF bulk capacitor plus several 22 uF and 10 uF ceramics on the 5 V rail.

> **Supply check needed.** The 5 V rail's storage and the MP2482 stage were sized
> around the E22-900M30S's 650 mA, not 1200 mA. Confirm the regulator,
> inductor, trace width, and the 12 V source can all sustain ~1.2 A peaks before
> running the 33S at full power. Starting at reduced power (see below) is the
> safe way to bring the board up.

### Positioning

The ATGM336H 5N GNSS module provides serial TX/RX and a PPS output, and is
supplied from the 5 V rail. The PCB exposes the signals needed for normal GPS
operation and time synchronization.

This module variant has no wake-up input, so there is no GNSS sleep control
line. `GPIO7` on this board is routed to the single-pin `H1` header as a spare
I/O rather than to the GNSS module.

### Motion and environmental sensing

- **GY-BMI160**: 6-axis inertial measurement module on the shared I2C bus, with
  its interrupt line brought out to the controller.
- **GY-BMP280**: barometric pressure and temperature sensor on the same I2C bus.
  BME280 and BMP280 modules can be used as practical alternatives when the
  module pinout and bus configuration match. BME280 additionally supports
  humidity measurement, so firmware must select the correct sensor type.

Note that the inertial module is wired for **I2C, not SPI**. Both sensors share
`GPIO17`/`GPIO18` with the RTC and the OLED.

Powering is deliberately asymmetric here. The GY-BMI160 takes 5 V (it has its
own regulator), and the GY-BMP280 is fed from the BMI160 module's 3.3 V output
through the `U12` solder bridge, which keeps the BMP280 within its 1.71-3.6 V
supply range without adding another regulator.

The GY and HW module names refer to inexpensive, widely available breakout
boards commonly sold through AliExpress and other component suppliers. Exact
PCB markings, regulators, pull-ups, and pin labels can vary by vendor, so check
the purchased module before installation.

### Timekeeping and display

- **PCF8563 HW-689 module**: RTC module with an interrupt output and its own
  backup cell. The module is supplied from the 5 V rail, which the PCF8563
  supports (its operating range is 1.0-5.5 V).
- **0.96-inch OLED module**: small local status display on the shared I2C bus.
  Other OLED sizes can be used in principle when the electrical interface,
  connector pin order, voltage levels, and mounting arrangement match. OLED
  modules with the same apparent pin names do not always use the same physical
  pin order; verify `VCC`, `GND`, `SDA`, and `SCL` before plugging one in.

The OLED socket is fed from the 5 V rail through the `U15` solder bridge. Many
4-pin 0.96-inch OLED modules have no regulator of their own and are specified
for 3.3 V, so check the module you intend to fit. If it is a 3.3 V-only type,
move the `U15` bridge to the 3.3 V side; the Pico's `3V3 OUT` rail is otherwise
almost unloaded on this board.

### Power and protection

Two 12 V entry paths feed a single node:

- DC barrel input through the `DC-005-A200` connector
- USB-C input through a `PDC004` PD decoy board, which negotiates 12 V from a
  USB-C power supply

Both connect to the same node, which then passes through an `SS54` Schottky
diode to become `VCC`. From there an `MP2482` step-down module produces the
board's 5 V rail. The `H2` header exposes the same `VCC` node as an alternative
input.

The 5 V rail carries all of the board's loads: the E22 radio, the GNSS module,
the BMI160, the PCF8563 module, the OLED, and the Pico's `VBUS` pins. It is
supported by a 470 uF bulk capacitor plus 22 uF and 10 uF ceramics. See the
supply note above: the 33S's ~1200 mA peak is roughly double what this rail was
originally sized for.

Two further rails are derived locally: the Pico's `3V3 OUT` feeds only the radio
control inverter, and the BMI160 module's own 3.3 V output feeds only the BMP280.

> **There is no battery or power-path circuitry on this board.** It is intended
> to run from a 12 V supply. The Pico's `VSYS` pins are unused, so a low or
> absent battery reading from firmware on this board is expected rather than a
> fault.

Because the PD and DC inputs land on the same node, confirm connector polarity
and the current capability of whichever supply you use before powering the
board. At 2 W the radio alone can pull ~1.2 A from the 5 V rail, so the 12 V
source must comfortably deliver that plus the logic and sensor loads.

## Main Pin Mapping

The following assignments are taken from the current schematic and are intended
to follow the T-Beam-S3 peripheral layout.

| Function      | ESP32-S3 GPIO | Notes                              |
| ------------- | ------------: | ---------------------------------- |
| LoRa DIO1     |         GPIO1 | E22 DIO1, radio interrupt          |
| LoRa BUSY     |         GPIO4 | E22 BUSY                           |
| LoRa NRST     |         GPIO5 | E22 reset, active low              |
| LoRa NSS / CS |        GPIO10 | E22 chip select                    |
| LoRa MOSI     |        GPIO11 | SPI data to radio                  |
| LoRa SCK      |        GPIO12 | SPI clock                          |
| LoRa MISO     |        GPIO13 | SPI data from radio                |
| GPS PPS       |         GPIO6 | GNSS pulse-per-second output       |
| GPS TX        |         GPIO8 | GNSS serial output (into ESP32)    |
| GPS RX        |         GPIO9 | GNSS serial input (from ESP32)     |
| I2C SDA       |        GPIO17 | OLED, RTC, BMI160, and BMP280      |
| I2C SCL       |        GPIO18 | OLED, RTC, BMI160, and BMP280      |
| RTC interrupt |        GPIO14 | PCF8563 interrupt                  |
| IMU interrupt |        GPIO33 | BMI160 interrupt                   |
| Spare I/O     |         GPIO7 | Routed to the `H1` header only     |

Two things are worth calling out because they are easy to get wrong from an
older board definition:

- **RF switching is not on a GPIO.** The E22 `TXEN`/`RXEN` pins are driven from
  the SX1262's DIO2 pin by the `U4` bridge and the `U3` inverter, not by the MCU.
  Do not assign a GPIO to `TXEN` or `RXEN` in firmware for this board.
- **The inertial module is on I2C.** `GPIO35`, `GPIO36`, and `GPIO37` are
  unconnected spare pins, not IMU SPI signals.

The remaining GPIOs available on the Pico module are unused on this revision:
`GPIO2`, `GPIO15`, `GPIO16`, `GPIO34`, `GPIO35`, `GPIO36`, `GPIO37`, `GPIO38`,
`GPIO39`, `GPIO40`, `GPIO41`, and `GPIO42`. Do not assume that an unlisted GPIO
is routed without checking the current netlist.

## Assembly Model

The board is intended to be assembled as a set of functional modules:

1. Install the ESP32-S3-Pico and the E22 radio.
2. Add the GPS, BMI160, BMP280, RTC, and OLED modules as required by the
   application.
3. Connect the desired power input and antenna.
4. Flash compatible firmware and validate each peripheral independently.

This approach makes it possible to create a minimal LoRa node, a GPS tracking
station, a sensor station, or a display-equipped development unit without
redesigning the core PCB.

## Flashing

**The carrier board's USB-C cannot flash this board.** It exists only to take
12 V from a USB-C PD supply through the `PDC004` decoy module; the netlist has no
USB data lines on it. Firmware is flashed through the **ESP32-S3-Pico module's
own USB-C connector**, which goes to a `CH343P` USB-serial bridge wired to the
SX1262's UART0, with a DTR/RTS auto-reset circuit, so no button press is needed.

This board carries an **ESP32-S3 N8R2**, so use the 8 MB partition layout. If you
previously flashed a 16 MB layout, erase the flash first.

See [`Review/FLASHING.md`](Review/FLASHING.md) for the full procedure, or use the
helper script:

```powershell
.\Review\flash.ps1 -Erase      # erase + flash the factory image
.\Review\flash.ps1 -Mode app   # update the application only, keep settings
```

## Repository Contents

- `PCB/SCH_Schematic1_1_2026-10-14.pdf`, `PCB/SCH_Schematic1_1_1-P1_2026-10-14.svg`: exported schematic
- `PCB/PCB_PCB1_1_2026-10-14.pdf`: PCB layout/manufacturing drawing
- `PCB/Netlist_Schematic1_1_2026-10-14.tel`: schematic netlist (text)
- `PCB/3D_PCB1_1_2026-10-14.step`: 3D model
- `PCB/InteractiveBOM_PCB1_1_2026-9-6.html`: interactive BOM and placement view
- `PCB/PickAndPlace_PCB1_1_2026_09_06.xlsx`: pick-and-place data
- `PCB/Gerber_PCB1_1_2026-09-06.zip`: Gerber fabrication package
- `PCB/Altium_T-mesh_2026-09-06.zip`: Altium project archive
- `PCB/ProPrj_T-mesh_2026-09-06.epro2`: EasyEDA Pro project archive
- `Datasheet/`: module and controller reference datasheets
- `Reference/`: Nano G1 Explorer, T-Beam-S3, and related hardware references
- `Review/`: design review notes, a Meshtastic board variant, and a build of it

> **Note on `ProPrj_T-mesh_2026-09-06.epro2`:** this archive bundles more than one
> board. Its project metadata describes a different, smaller nRF52840-based
> design (48 x 55 mm, E22-400M30S), while the ESP32-S3 board described in this
> README is also present inside it. The Gerber and Altium archives in `PCB/`
> contain only this board. If you re-export manufacturing files, confirm you
> have the ESP32-S3 project selected.

## Module Reference Images

The repository includes reference photos for the module types used by this
design. They are visual references, not manufacturing specifications; modules
from different vendors can have different labels, pin orders, regulators, or
mounting details.

| Module | Reference image |
| --- | --- |
| ESP32-S3-Pico | [ESP32-S3-Pico details](Datasheet/module_pics/ESP32-S3-Pico-details-size.jpg) |
| E22-900M30S | [E22-900M30S](Datasheet/module_pics/E22-900M30S.webp) |
| ATGM336H 5N | [ATGM336H 5N](Datasheet/module_pics/ATGM336H%205N.png) |
| BMI160 | [BMI160](Datasheet/module_pics/BMI160.webp) |
| BME280 / BMP280 | [BME/BMP280](Datasheet/module_pics/BME,BMP280.webp) |
| PCF8563 | [PCF8563](Datasheet/module_pics/PCF8563.webp) |
| OLED | [OLED 12864](Datasheet/module_pics/OLED_12864.webp) |
| DC-DC power module | [MP2482 / SY8205 5 A](Datasheet/module_pics/DC-DC%20MP2482%20SY8205%205A.webp) |

## Firmware Compatibility

The board is designed to be close to the T-Beam-S3 pinout so that existing
T-Beam-S3 firmware can serve as a starting point. Firmware still needs to match
the actual module variants, radio power-control behavior, display/RTC bus
configuration, and board power arrangement. Validate radio operation, GPS serial
direction, sensor bus selection, and interrupt polarity on the assembled
hardware.

The one thing that cannot be carried over unchanged is the RF switch setup:
firmware must configure the SX1262 to drive DIO2 as its RF switch control (for
example `radio.setDio2AsRfSwitch(true)` in RadioLib). Firmware that expects to
toggle `TXEN`/`RXEN` from MCU GPIOs will not work on this board, because those
pins are not connected to the controller.

The second board-specific item is transmit power. Because the E22-900M33S has
its own PA — and that PA must not see more than 9 dBm — firmware must know the
PA gain so it can hold the SX1262 down to the correct chip power. Meshtastic
carries a table for this module behind the `EBYTE_E22_900M33S` build define, and
the included variant uses it.

Getting this wrong is not a performance issue, it is a **damage** risk: firmware
that ignores the PA gain will drive the SX1262 to its own +22 dBm maximum and
overdrive the input by about 13 dB. Use `EBYTE_E22_900M33S`, never
`EBYTE_E22_900M30S`, for the module fitted here.

This board is fitted with an **ESP32-S3 N8R2** module (8 MB flash, 2 MB PSRAM).
A firmware build for it must use an 8 MB partition table; the 16 MB layout used
by the stock ESP32-S3-Pico board definition does not match this flash size.

> **If you previously flashed a build that used the 16 MB partition layout,**
> erase the whole flash before flashing the 8 MB build
> (`esptool.py erase_flash`, or hold BOOT while connecting). Switching partition
> tables changes where the NVS area lives, so an in-place update would leave the
> saved Meshtastic configuration reading from the wrong region.

### Verified Meshtastic build

A complete, bench-verified Meshtastic port is included in this repository:

| Where | What |
| --- | --- |
| `Review/meshtastic-variant/tmesh-s3-e22/` | The board variant (`variant.h`, `platformio.ini`) |
| `Review/meshtastic-variant/boards/` | Board definition JSON (8 MB flash, PSRAM disabled) |
| `Review/meshtastic-variant/bmi160-driver/` | BMI160 accelerometer driver (not in upstream) |
| `Review/meshtastic-variant/upstream-modified/` | Modified upstream files, for reference |
| `Review/firmware/` | Prebuilt, verified binaries |

**What is verified working on hardware:**

- Boot, USB serial, screen (SSD1306), and the BOOT button (click navigates,
  long-press opens the menu)
- SX1262 radio initialisation, DIO2 RF switch, and the 33S power table
  (chip held to 5 dBm, well under the module's 9 dBm input limit)
- Mesh traffic: receiving, decrypting, routing, rebroadcasting, and transmitting
- GPS fix and NTP-grade time from the ATGM336H
- BMP280 environmental telemetry, PCF8563 RTC, BMI160 motion sensing

**Board-specific items firmware must get right:**

1. **RF switching** — DIO2 must be configured as the RF switch control
   (`SX1262_DIO2_AS_RF_SWITCH`). The MCU cannot toggle `TXEN`/`RXEN`.
2. **Transmit power** — see the warning above; use `EBYTE_E22_900M33S`.
3. **PSRAM must stay disabled** — the on-die 2 MB PSRAM fails to initialise in
   the bootloader on this module, causing a reboot loop. The board JSON
   therefore declares no PSRAM. See `Review/PSRAM-FIX-SOLVED.md`.
4. **GPS pins** — `GPS_RX_PIN 8` / `GPS_TX_PIN 9` (named from the ESP32's side).
5. **RTC** — needs `PCF8563_RTC 0x51` plus the `PCF8xRTC` library dependency.

Read the full write-ups for the reasoning and the evidence behind each item:

| Document | Subject |
| --- | --- |
| [MESHTASTIC-VARIANT.md](Review/MESHTASTIC-VARIANT.md) | Variant adaptation and build steps |
| [FLASHING.md](Review/FLASHING.md) | How to flash, with a working script |
| [TX-POWER-AUDIT.md](Review/TX-POWER-AUDIT.md) | Why the radio cannot overdrive its PA |
| [GPS-FIX-SOLVED.md](Review/GPS-FIX-SOLVED.md) | GPS pin direction and NMEA fallback |
| [BMI160-DRIVER.md](Review/BMI160-DRIVER.md) | The added accelerometer driver |
| [PSRAM-FIX-SOLVED.md](Review/PSRAM-FIX-SOLVED.md) | The PSRAM boot-loop root cause |
| [CURRENT-STATUS.md](Review/CURRENT-STATUS.md) | Current state and known trade-offs |
| [FEATURES-AND-LIMITS.md](Review/FEATURES-AND-LIMITS.md) | What the phone app can see and control |
| [HARDWARE-SELFTEST.md](Review/HARDWARE-SELFTEST.md) | Per-peripheral self-test results |
| [UPSTREAM-CONTRIBUTION.md](Review/UPSTREAM-CONTRIBUTION.md) | How to submit the driver upstream |

## Safety and RF Notes

- Use an antenna rated for the selected 868/915 MHz region and transmit power.
- Keep the antenna away from metal and provide a proper RF ground/reference.
- Do not apply more than the E22 module's rated supply voltage.
- **Never drive the E22-900M33S's RF chip above 9 dBm** — use the
  `EBYTE_E22_900M33S` power table, not the 900M30S one.
- The radio can require approximately **1200 mA** during 2 W transmission; use a
  supply with sufficient current margin and verify the 5 V rail can sustain it.
- 2 W output requires a licence or other authorization in most regions; verify
  local regulations before enabling full power.
- The 33S module's blocking power is 10 dBm — avoid operating two high-power
  nodes facing each other at very close range.
- Verify local radio regulations before operating at high transmit power.

## Status

This is an active hardware design. The manufacturing package and module list
describe the 2026-09-06 revision; mechanical fit, thermal behavior, and RF range
still require validation on assembled hardware.

On the firmware side, a Meshtastic board variant for this design has been built
and verified to compile (`Review/meshtastic-variant/`, `Review/firmware/`). It
has not yet been run on assembled hardware; see
`Review/MESHTASTIC-VARIANT.md` for what to check first on a real board.
