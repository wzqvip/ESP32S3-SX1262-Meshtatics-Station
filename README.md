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
> This repository contains the current design files. Check the schematic and
> the selected module datasheets before ordering or powering a board.

## Highlights

- Waveshare ESP32-S3-Pico main controller, or a compatible version
- SX1262-based Ebyte E22-900M30S 1 W LoRa radio
- Interchangeable E22-family radio footprint for compatible variants
- GPS, motion, pressure, RTC, and OLED modules
- T-Beam-S3-oriented GPIO assignments
- USB-C and DC input power options
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

### LoRa radio

The default radio is the [Ebyte E22-900M30S](https://www.cdebyte.com/products/E22-900M30S),
an SX1262-compatible LoRa module for the US 900 MHz band with an integrated PA
and LNA. It is rated for up to 1 W transmit power. The module is connected
through SPI and has separate control signals for reset, busy, DIO1, TX enable,
and RX enable. The schematic also includes an SN74LVC1G14 inverter for the
radio control path.

The PCB is intended to support compatible modules from the E22 family, such as
the E22-400M22S, E22-900M22S, and E22-900M33S, provided the module dimensions,
pinout, supply requirements, RF band, and control behavior match. This is a
hardware-level compatibility target, not a guarantee that every E22 variant
will work without firmware or RF configuration changes.

The E22 module uses 3.3 V logic and accepts a 2.5-5.5 V supply according to the
E22-900M30S datasheet. The 30S 1 W version is the intended lower-power,
unlicensed-use option where permitted by local regulations. The
[E22-900M33S](https://www.cdebyte.com/products/E22-900M33S) is a 2 W variant
and may require a radio license or other authorization; do not use it as a
drop-in assumption. Its high-power transmit mode can draw substantial
instantaneous current, so use a stable supply, a suitable antenna, and an
appropriate enclosure.

### Positioning

The ATGM336H 5N GNSS module provides serial TX/RX, a PPS output, and a wake-up
control signal. The PCB exposes the signals needed for normal GPS operation and
time synchronization.

### Motion and environmental sensing

- **GY-BMI160**: 6-axis inertial measurement module connected through the
  dedicated sensor SPI signals, with an interrupt line.
- **GY-BMP280**: barometric pressure and temperature sensor on the sensor bus.
  BME280 and BMP280 modules can be used as practical alternatives when the
  module pinout and bus configuration match. BME280 additionally supports
  humidity measurement, so firmware must select the correct sensor type.

The GY and HW module names refer to inexpensive, widely available breakout
boards commonly sold through AliExpress and other component suppliers. Exact
PCB markings, regulators, pull-ups, and pin labels can vary by vendor, so check
the purchased module before installation.

### Timekeeping and display

- **PCF8563 HW-689 module**: battery-backed RTC module with an interrupt output.
- **0.96-inch OLED module**: small local status display on the shared I2C bus.
  Other OLED sizes can be used in principle when the electrical interface,
  connector pin order, voltage levels, and mounting arrangement match. OLED
  modules with the same apparent pin names do not always use the same physical
  pin order; verify `VCC`, `GND`, `SDA`, and `SCL` before plugging one in.

### Power and protection

The schematic provides several power entry and conversion blocks:

- USB-C 5 V input
- DC barrel input through the `DC-005-A200` connector
- MP2482 power conversion stage for the external DC input path
- SS54 protection diode
- Local bulk and ceramic decoupling capacitors
- Separate 5 V and 3.3 V power domains used by the radio, controller, and
  sensor modules

The current schematic labels the DC input path as 12 V by default and also
provides a 5 V input path. The power arrangement prevents the DC input from
back-feeding the USB-C connector. When power is supplied through USB-C, the DC
connector becomes a power output rather than a second input. Confirm the final
power configuration, connector polarity, output capability, and regulator
thermal limits on the assembled revision before use.

## Main Pin Mapping

The following assignments are taken from the current schematic and are intended
to follow the T-Beam-S3 peripheral layout.

| Function      | ESP32-S3 GPIO | Notes                         |
| ------------- | ------------: | ----------------------------- |
| LoRa DIO1     |         GPIO1 | E22 DIO1                      |
| LoRa BUSY     |         GPIO4 | E22 BUSY                      |
| LoRa NRST     |         GPIO5 | E22 reset, active low         |
| LoRa NSS / CS |        GPIO10 | E22 chip select               |
| LoRa MOSI     |        GPIO11 | SPI data to radio             |
| LoRa SCK      |        GPIO12 | SPI clock                     |
| LoRa MISO     |        GPIO13 | SPI data from radio           |
| GPS PPS       |         GPIO6 | GNSS pulse-per-second output  |
| GPS WAKEUP    |         GPIO7 | GNSS module control           |
| GPS TX        |         GPIO8 | GNSS serial output            |
| GPS RX        |         GPIO9 | GNSS serial input             |
| I2C SDA       |        GPIO17 | OLED, RTC, and I2C sensor bus |
| I2C SCL       |        GPIO18 | OLED, RTC, and I2C sensor bus |
| RTC interrupt |        GPIO14 | PCF8563 interrupt             |
| IMU interrupt |        GPIO33 | BMI160 interrupt              |
| IMU SPI MOSI  |        GPIO35 | BMI160 SPI                    |
| IMU SPI SCK   |        GPIO36 | BMI160 SPI                    |
| IMU SPI MISO  |        GPIO37 | BMI160 SPI                    |

The exact IMU chip-select and any unused radio DIO2 routing should be taken
from the schematic when writing a new board definition. Do not assume that an
unlisted GPIO is free without checking the current revision.

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

## Repository Contents

- `PCB/SCH_Schematic1_1_2026-09-06.pdf`: exported schematic
- `PCB/PCB_PCB1_1_2026-09-06.pdf`: PCB layout/manufacturing drawing
- `PCB/InteractiveBOM_PCB1_1_2026-9-6.html`: interactive BOM and placement view
- `PCB/PickAndPlace_PCB1_1_2026_09_06.xlsx`: pick-and-place data
- `PCB/Gerber_PCB1_1_2026-09-06.zip`: Gerber fabrication package
- `PCB/Altium_T-mesh_2026-09-06.zip`: Altium project archive
- `PCB/ProPrj_T-mesh_2026-09-06.epro2`: project file
- `Datasheet/`: module and controller reference datasheets
- `Reference/`: T-Beam-S3 reference documents and related hardware references

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
T-Beam-S3 firmware can serve as a starting point. Firmware still needs to
match the actual module variants, radio power-control behavior, display/RTC
bus configuration, and board power arrangement. Validate radio operation,
GPS serial direction, sensor bus selection, and interrupt polarity on the
assembled hardware.

## Safety and RF Notes

- Use an antenna rated for the selected 868/915 MHz region and transmit power.
- Keep the antenna away from metal and provide a proper RF ground/reference.
- Do not apply more than the E22 module's rated supply voltage.
- The radio can require approximately 650 mA during high-power transmission;
  use a supply with sufficient current margin.
- Verify local radio regulations before operating at high transmit power.

## Status

This is an active hardware design. The manufacturing package and module list
describe the 2026-09-06 revision; mechanical fit, thermal behavior, RF range,
and complete firmware compatibility still require validation on assembled
hardware.
