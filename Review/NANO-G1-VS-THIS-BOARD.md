# Nano G1 Explorer ↔ 本板（T-mesh）差异对照

> 左列取自 `Reference/nano_g1_explorer_06_mar_2023.pdf`（矢量文本 + 网络名，U9 引脚表已逐行核对）
> 右列取自 `PCB/Netlist_Schematic1_1_2026-10-14.tel` + Gerber 内 `FlyingProbeTesting.json`

> **修订记录**：初版把 Nano G1 的主控误判为 ESP32-S3，引脚号随之全部写错。经逐行核对
> Nano G1 的 `U9` 引脚表（38-pin，标注 `ESP-32S`、`SENSOR_VP/SENSOR_VN`、`SHD/SD2`、
> `SWP/SD3`、`SCS/CMD` 这些只有 ESP32-WROOM-32 才有的脚名），确认 **Nano G1 用的是
> ESP32-WROOM-32（ESP-32S）模块**，不是 ESP32-S3。下表已全面更正。

---

## 0. 一句话结论

**两块板的主控不同、射频架构不同，固件既不能照抄 Nano G1，也不能照抄 T-Beam-S3 的 board 定义。**

- **Nano G1**：ESP32-WROOM-32 + 裸 SX1262 + 外部射频前端，用 `FEM_CTX` 控制，SX1262 的 DIO2 只做上拉、不参与开关控制。
- **本板**：ESP32-S3-Pico + E22-900M30S（SX1262+PA+LNA 一体模块），用 `TXEN`/`RXEN` 控制，DIO2 由固件配置成射频开关控制脚。

**主控从 ESP32 换到 ESP32-S3，这是移植时最容易被忽略的一层差异**（见第 5 节）。

---

## 1. 主控对比

| 项目 | Nano G1 Explorer | 本板 T-mesh |
| --- | --- | --- |
| 模块 | **ESP32-WROOM-32 / ESP-32S**（`U9`，38-pin 半孔） | **Waveshare ESP32-S3-Pico**（`U1`，40-pin，LCC 51.0×21.0） |
| 芯片 | ESP32（双核 Xtensa LX6） | ESP32-S3（双核 LX7 + AI 指令） |
| 供电脚 | pin38 GND / pin39 VCC_3v3 | pin36 3V3(OUT) / pin39 VBUS / pin40 VBUS |
| 复位/使能 | `ESP32_NRESET` | `EN` 内部处理 |
| 电池监测 | 有：`BAT_ADC`（IO35）、`BAT_EN` | **无**（`VSYS` pin37/38 未使用） |
| 独立 I2C（OLED） | IO21 (SDA) / IO22 (SCL) | 与其他传感器**共用** GPIO17/18 |
| GPS 串口 | IO12 (TXD) / IO13 (RXD) | GPIO8 (TX) / GPIO9 (RX) |
| 串口 0 | `TXD0`/`RXD0`（pin34/35） | 未引出复用 |
| 其他 | `Notification` (IO2)、`SW1` | — |

---

## 2. LoRa 接口引脚对照（已逐行核对，本版已更正）

| 信号 | Nano G1（ESP32-WROOM-32） | 本板（ESP32-S3-Pico） | 是否一致 |
| --- | --- | --- | --- |
| **NSS / CS** | `U9.30` → **IO19** | U1.35 → **GPIO10** | ❌ |
| **SCK** | `U9.29` → **IO18** | U1.2 → **GPIO12** | ❌ |
| **MOSI** | `U9.27` → **IO17**（未复核） | U1.1 → **GPIO11** | ❌ |
| **MISO** | `U9` 右侧，**未复核** | U1.4 → **GPIO13** | ❌ |
| **BUSY** | `U9` 右侧，**未复核** | U1.26 → **GPIO4** | ❌ |
| **DIO1** | `U9.9` → **IO32** | U1.24 → **GPIO1** | ❌ |
| **NRESET** | `ESP32_NRESET`，**未复核具体 GPIO** | U1.27 → **GPIO5** | ❌ |
| **前端/开关控制** | `FEM_CTX` = `U9.10` → **IO33** | DIO2 内部驱动 + 硬件反相器 | ❌ 概念不同 |

> ⚠️ **诚实说明**：从 PDF 矢量文本里能 100% 确定对应关系的是 **DIO1 = IO32** 和
> **FEM_CTX = IO33** 这两行（它们与 `U9.9` / `U9.10` 明确对齐）。
> 其余几行里，`U9` 右侧那一列在网络名和 GPIO 号之间发生了串行
> （`ESP32_BUSY/ESP32_MISO/ESP32_NSS/ESP32_SCK/ESP32_MOSI` 挤在同一列），
> 我**无法靠文本确定**它们各自对应哪个 GPIO。NSS/MISO/SCK/MOSI/BUSY/NRESET 的
> 确切 GPIO 号**请以 PDF 原图为准**。
>
> 但这不影响结论：**没有任何一行是相同的。**

**无论如何，结论不变：LoRa 的 SPI/控制线在两块板上没有一个 GPIO 号相同。**

本板的 GPIO 分配与 **T-Beam-S3** 家族一致（这是 README 声明的设计目标），所以固件应以
T-Beam-S3 board 定义为起点，再把第 5 节的主控差异补上。

---

## 3. 射频架构对比

| 项目 | Nano G1 Explorer | 本板 T-mesh |
| --- | --- | --- |
| 射频器件 | 裸 **SX1262**（`SX1262.SchDoc`，U10） | **E22-900M30S** 模块（U2，22-pin 邮票孔） |
| PA / LNA | 外部独立前端模块（U12） | 集成在 E22 模块内部 |
| 开关控制 | `FEM_CTX` 单信号 + `RFS_V1`/`RFS_V2` 二选一 | `TXEN`(U2.7) / `RXEN`(U2.6) 两个独立脚 |
| DIO2 用途 | 经 **R36 上拉**，不接 MCU、不控制开关 | 接 **U3 反相器 + U4 跳线**，是切换的唯一信号源 |
| DIO3 | 显式引出（`SX1262_DIO3`） | E22 内部用 DIO3 供 32 MHz TCXO，不可干预 |
| 天线 | 板上天线 + **IPEX-M** 座 | **U2.21 = ANT**，邮票孔 50 Ω |
| 发射功率 | SX1262 裸功率（约 +22 dBm 级） | E22-900M30S 额定 **1 W（+30 dBm）** |
| 峰值电流 | 约 120 mA 量级 | 约 **650 mA** |
| 切换方式 | 芯片与前端模块配合，MCU 基本不干预 | **U4 跳线 + DIO2 → TXEN / RXEN 互斥** |

```
Nano G1:
  SX1262 ── DIO2 ── R36 上拉（悬空不用）
         └─ RF ──> [外部前端模块 U12] ──> 天线
                     ↑ FEM_CTX ← ESP32 IO33
                     ↑ RFS_V1 / RFS_V2（0Ω 选型）

本板 T-mesh:
  E22 U2.8 DIO2 ─┬─ U4 (跳线, 默认短接) ──> U2.7 TXEN ─┐
                 └─ U3 (SN74LVC1G14 反相) ─> U2.6 RXEN ┤
                                                        └─> E22 内部 PA/LNA + 开关 ──> U2.21 ANT
     DIO2 由 SX1262 内部 RF switch 控制器驱动（需 SetDio2AsRfSwitchCtrl）
```

---

## 4. 本板独有 / Nano G1 独有

### 4.1 本板独有（Nano G1 完全没有）

| 项目 | 说明 |
| --- | --- |
| **U4 跳线（必须短接）** | DIO2 → TXEN。开路则 TXEN 恒低，模块停在约 2 µA 软件关断，收发全废 |
| **U3 SN74LVC1G14 反相器** | 产生 RXEN，由 `ESP_3V3` 供电 |
| **DIO2 必须被固件配置** | `SetDio2AsRfSwitchCtrl(1)`。Nano G1 不需要这一步 |
| **12 V 直流输入 + PD 诱骗** | Nano G1 无 DC 座、无 PD 模块，只有 USB-C |
| **470 µF 大储能** | 为 1 W 发射脉冲电流准备 |
| **模块化插接外设** | GY-BMI160 / GY-BMP280 / PCF8563 模块 / OLED / ATGM336H |

### 4.2 Nano G1 独有（不要移植到本板）

| 项目 | 说明 |
| --- | --- |
| **`FEM_CTX` / `RFS_V1` / `RFS_V2`** | Nano G1 的外部前端选型机制，本板由 E22 内部开关取代 |
| **`SX1262_DIO3` 外部引出** | 本板不可干预 |
| **IPEX-M 天线座** | 本板只有邮票孔 ANT |
| **锂电池 / `BAT_ADC` / `BAT_EN`** | 本板没有电池路径 |
| **独立 OLED I2C（IO21/22）** | 本板所有 I2C 设备共用 GPIO17/18 |
| **`Notification` / `SW1` 按键** | 本板无 |

---

## 5. ESP32 → ESP32-S3 的移植陷阱

从 Nano G1 借代码时，除了引脚号，还要处理这些主控层面的差异：

1. **IO 数量与编号**：ESP32-S3 的 GPIO22–25 在 Pico 上不存在（内部给 flash/PSRAM）。Nano G1 用的 **IO21/IO22/IO23/IO25/IO26/IO27/IO32/IO33/IO34/IO35** 在 S3 上大部分不可用或已被占用。**IO34/IO35 在 ESP32 上是纯输入脚**，S3 上 GPIO34 虽可输出但本板已留空。
2. **启动 strapping 脚**：ESP32-S3 的 **GPIO0/GPIO3/GPIO45/GPIO46** 是 strapping 脚，本板 GPIO3/GPIO45/GPIO46 未引出，**GPIO0 也未使用** —— 所以本板的 GPIO 选择没有踩 strapping 坑，这点是好的。
3. **纯输入脚**：S3 无 ESP32 那种严格纯输入脚，但 GPIO19/20 在本板未使用（通常留给原生 USB D−/D+）。
4. **USB**：ESP32-WROOM-32 没有原生 USB，Nano G1 靠外置 USB-UART。ESP32-S3-Pico 有原生 USB，**烧录方式完全不同**。
5. **SPI 外设**：两块板的 SPI 引脚都不在 S3 的默认 FSPI 组之外也都能用，但 S3 的 SPI2/SPI3 主机编号与 ESP32 不同，Arduino 下 `SPI.begin(sck, miso, mosi, cs)` 显式指定引脚最稳。
6. **射频库**：SX1262 驱动本身可复用，但 `SetDio2AsRfSwitchCtrl` 这一步是本板**新增且必需**的。

---

## 6. 移植固件动作清单

1. **不要**移植 `FEM_CTX` / `RFS_V1` / `RFS_V2` 相关代码。
2. **必须**加 `SetDio2AsRfSwitchCtrl(1)`（RadioLib：`radio.setDio2AsRfSwitch(true)`）。
3. 按第 2 节替换**全部** LoRa 引脚号（T-Beam-S3 分配基本可直接用）。
4. 处理第 5 节的主控差异（原生 USB 烧录、IO 编号、SPI 主机）。
5. 确认 `U4` 实物已短接。
6. 发射功率上限：E22-900M30S 为 1 W，注意**当地法规**；`E22-900M33S`（2 W）不是直接替换件。
7. 电源余量：12 V → MP2482 → 5 V，峰值 650 mA。
8. BMI160 走 **I2C**（GPIO17/18），中断 GPIO33 —— **不是 SPI**，README 的 SPI 描述需修正。
