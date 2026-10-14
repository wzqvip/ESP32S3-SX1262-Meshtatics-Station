# Meshtastic 固件适配说明（T-mesh）

> 目标板：ESP32-S3-Pico（Waveshare，16 MB flash / 8 MB PSRAM）+ Ebyte E22-900M30S（1 W）
> 上游：`meshtastic/firmware` master 分支
> 变体路径：`variants/esp32s3/diy/tmesh-s3-e22/`

---

## 0. 结论先说

Meshtastic **原生就支持**这块板子的射频拓扑，不需要改任何上游源码。

上游 `src/mesh/SX126xInterface.cpp` 里针对 SX126x 的射频开关有两条明确分支：

```cpp
#ifdef SX126X_DIO2_AS_RF_SWITCH
    bool dio2AsRfSwitch = true;
#else
    bool dio2AsRfSwitch = false;
#endif
    res = lora.setDio2AsRfSwitch(dio2AsRfSwitch);
...
    lora.setRfSwitchPins(SX126X_RXEN, SX126X_TXEN);
```

- **`SX126X_DIO2_AS_RF_SWITCH`** → 让 SX1262 自己驱动 DIO2。**这是本板要用的。**
- **`SX126X_TXEN` / `SX126X_RXEN`** → MCU 直接控制射频开关。**本板不能用。**

而且上游那段注释几乎就是在描述本板的拓扑：

> "Typically, the RF switch on SX126x boards is controlled by two signals, which are
> negations of each other... On some boards, there is no hardware negation between
> CTRL and ¬CTRL, but CTRL is internally connected to DIO2, and DIO2's switching is
> done by the SX126X itself, so the MCU can't control ¬CTRL at exactly the same time."

**而本板比这个更好**：不仅 CTRL 内部连到 DIO2，**¬CTRL(RXEN) 还是用一颗 SN74LVC1G14 硬件反相出来的**。
也就是说上游说的那个"suboptimal design"的妥协（把 ¬CTRL 常拉高、靠 RF 开关特性侥幸工作），
本板用一颗反相器从根上避免了。**这是本板相对 T-Beam-1W 的一个设计优点。**

---

## 1. 三条路线的取舍（为什么这么配）

上游有三个同类参照，很容易选错：

| 上游变体 | 射频器件 | 开关控制方式 | 对本板适用性 |
| --- | --- | --- | --- |
| `variants/esp32s3/t-beam-1w` | SX1262 + 外部 1W PA | `SX126X_DIO2_AS_RF_SWITCH` + `SX126X_RXEN 21` | ⚠️ 思路对，但它多一个 MCU 控制的 LNA 脚 |
| `variants/esp32s3/diy/t-energy-s3_e22` | **同为 E22** | `SX126X_TXEN 10` + `SX126X_RXEN 12`（MCU 直控） | ❌ 同模块，但控制方式相反，**不能照抄** |
| **本板 tmesh-s3-e22** | E22-900M30S | **仅 `SX126X_DIO2_AS_RF_SWITCH`** | ✅ |

**关键的坑**：`t-energy-s3_e22` 也是 E22 模块，搜索"Meshtastic E22"最容易先找到它。
但它的板子把 TXEN/RXEN 接到了 MCU GPIO，而**本板没有**。

本板真值表：

| DIO2 | TXEN (U2.7) | RXEN (U2.6) | 路径 |
| --- | --- | --- | --- |
| 1 | 1（经 U4 跳线直连） | 0（经 U3 反相） | 发射 |
| 0 | 0 | 1 | 接收 |

所以本板只定义 `SX126X_DIO2_AS_RF_SWITCH`，**绝对不能**定义 `SX126X_TXEN`/`SX126X_RXEN` ——
那两个脚根本没连到 MCU，定义了会让 `setRfSwitchPins()` 去操作不存在的 GPIO，与芯片自身的
DIO2 驱动打架。

---

## 2. Variant 文件

### 2.1 `platformio.ini`

```ini
[env:tmesh-s3-e22]
extends = esp32s3_base
board = esp32-s3-pico
board_level = extra                 ; DIY 变体, 不占官方 hw_model 编号

; ★ 本板是 ESP32-S3 N8R2 (8MB flash)。
;   boards/esp32-s3-pico.json 默认按 16MB 配分区表, 必须覆盖。
board_upload.flash_size = 8MB
board_build.partitions = default_8MB.csv
board_build.arduino.memory_type = qio_opi   ; N8R2 = Quad flash + Octal PSRAM

build_flags =
  ${esp32s3_base.build_flags}
  -D ARDUINO_USB_MODE=1        ; ★ 必须与下一行成对出现
  -D ARDUINO_USB_CDC_ON_BOOT=1
  -I variants/esp32s3/diy/tmesh-s3-e22
  -D TMESH_S3_E22
  -D EBYTE_E22_900M30S         ; ★ 激活上游的 E22-900M30S 功率映射表 (见第 6 节)
  -D TMESH_LOW_POWER_TEST      ; ★ 首次上电: 小功率模式, 验证后注释掉
```

`board_level = extra` 是上游 DIY 变体的标准做法（见 `t-energy-s3_e22`），
让变体不进入默认构建、也不需要注册新的 `hw_model` 枚举值。

> **N8R2 的分区表必须覆盖。** `boards/esp32-s3-pico.json` 里写的是
> `"partitions": "default_16MB.csv"`、`"flash_size": "16MB"`，那是 16MB 版 Pico。
> 用 N8R2 而不覆盖的话会按 16MB 布局，可能刷到不存在的地址。
> 覆盖成 8MB 之后 app 分区是 3342336 B（16MB 布局下是 2424832 B），
> Flash 占用从 94.6% 降到 **68.6%**，余量反而更充裕。

### 2.2 构建踩坑：`USBSerial was not declared in this scope`

第一次构建失败在这里：

```
cores/esp32/HardwareSerial.h:447:16: error: 'USBSerial' was not declared in this scope
  447 | #define Serial USBSerial
```

原因：`ARDUINO_USB_CDC_ON_BOOT=1` 会把 `Serial` 映射成 `USBSerial`，
而 `USBSerial` 这个对象**只在 `ARDUINO_USB_MODE=1` 时才被编译出来**。
`boards/esp32-s3-pico.json` 的 `extra_flags` 里没有这两个宏，所以必须在
variant 的 `build_flags` 里自己补**一对**，只补一个就报这个错。

> 上游 `t-energy-s3_e22` 处理同一个问题的方向相反：
> 它的 devkit 板级 JSON 自带 `ARDUINO_USB_MODE=1`，而它想要 OTG 模式，
> 于是用 `build_unflags` 把 `ARDUINO_USB_MODE=1` 撤掉再改成 `0`。
> **同一对宏，两种板子两种改法 —— 关键是要成对。**

### 2.3 射频部分（variant.h 核心）

```cpp
#define USE_SX1262

#define LORA_CS 10     // U2.19 NSS
#define LORA_SCK 12    // U2.18 SCK
#define LORA_MOSI 11   // U2.17 MOSI
#define LORA_MISO 13   // U2.16 MISO
#define LORA_RESET 5   // U2.15 NRST
#define LORA_BUSY 4    // U2.14 BUSY
#define LORA_DIO1 1    // U2.13 DIO1

// ★★★ 本板核心 ★★★
#define SX126X_DIO2_AS_RF_SWITCH
// 故意没有 SX126X_TXEN / SX126X_RXEN

#define SX126X_DIO3_TCXO_VOLTAGE 1.8   // E22 内部 DIO3 供 32MHz TCXO
#define SX126X_MAX_POWER 22
```

### 2.4 其余外设

```cpp
#define I2C_SDA 17
#define I2C_SCL 18
#define HAS_SCREEN 1
#define USE_SSD1306      // 若花屏改为 USE_SH1106
#define HAS_GPS 1
#define GPS_TX_PIN 8
#define GPS_RX_PIN 9
#define PIN_GPS_PPS 6    // 正确宏名是 PIN_GPS_PPS
#define GPS_BAUDRATE 9600
```

**故意不定义**（本板没这些硬件，定义了会让 Meshtastic 去操作不存在的引脚）：
`BATTERY_PIN`、`ADC_CHANNEL`、`ADC_MULTIPLIER`、`BUTTON_PIN`、NeoPixel、蜂鸣器、SD 卡、风扇。

---

## 3. 踩过的坑（上游宏名与实际不符的地方）

| 我最初写的 | 实际正确的 | 说明 |
| --- | --- | --- |
| `GPS_1PPS_PIN` | **`PIN_GPS_PPS`** | `GPS.cpp:2092` 用的是 `#ifdef PIN_GPS_PPS`。`GPS_1PPS_PIN` 是 T-Beam-1W variant 里的**死宏**，全仓无人引用 |
| `OLED_WIDTH` / `OLED_HEIGHT` | **不需要定义** | `SharedUIDisplay.h:56` 定义为 `SCREEN_WIDTH display->getWidth()`，尺寸由显示驱动自己给出。这两个宏全仓无人引用 |

> 教训：上游 variant 文件里有不少"看着有用其实没人用"的宏（`t-beam-1w` 里就有）。
> **加宏之前先 grep 一遍源码确认有消费方**，否则你以为配上了，其实没生效。

---

## 4. 编译（已实际验证通过）

```powershell
# 环境: Python 3.12 venv + PlatformIO Core 6.2.0
python -m venv venv
venv\Scripts\python.exe -m pip install -U platformio
# 把 variants/esp32s3/diy/tmesh-s3-e22/ 放进固件仓库后:
$env:PLATFORMIO_CORE_DIR='D:\meshtastic-build\pio-core'
venv\Scripts\pio.exe run -e tmesh-s3-e22 -d <firmware-repo>
```

- 平台：`platformio/espressif32@6.13.0`（上游 `variants/esp32/esp32-common.ini` 固定）
- 首次构建需下载完整 ESP32 工具链（实测约 3.4 GB），首轮约 20 分钟（含失败那次）
- 修复 USB 宏后：**`SUCCESS`，耗时 15 分 12 秒**

### 4.1 构建结果

| 项 | 值 |
| --- | --- |
| 状态 | **SUCCESS** |
| 上游版本 | 2.8.2.04cb888（`version.properties`: major 2 / minor 8 / build 2） |
| RAM | 30.6%（100212 / 327680 B） |
| Flash | **68.6%**（2293051 / 3342336 B） |
| app bin | 2.19 MB |

> **Flash 余量问题已解决。** 早先用 16 MB 分区表时占用 94.6%（只剩约 132 KB），
> 改成 N8R2 正确的 8 MB 分区表后，app 分区达到 3342336 B，占用降到 **68.6%**，
> 余量约 1 MB。开音频 / PAX counter / Web 服务器都有空间了。

产物已随本仓库附上（`Review/firmware/`）：

- `tmesh-s3-e22-factory.bin` — 合并镜像，**从 0x0 烧录**，首次刷机用
- `tmesh-s3-e22-app.bin` — 应用镜像，OTA / 从 0x10000 烧录用
- `tmesh-s3-e22-littlefs.bin` — 文件系统镜像

### 4.2 配置生效的验证（重要，不要只信"编译通过"）

"能编译"不等于"配置生效了"。这里做了**两道**独立硬验证。

#### 验证一：变体头文件确实被包含

用**编译器自己生成的依赖文件**：

```powershell
Select-String -Path '.pio\build\tmesh-s3-e22\src\mesh\SX126xInterface.cpp.d' -Pattern 'tmesh-s3-e22'
```

输出中确实包含：

```
.pio/build/tmesh-s3-e22/src/mesh/SX126xInterface.cpp.o: \
variants/esp32s3/diy/tmesh-s3-e22/variant.h \
```

**这证明射频接口那个编译单元真的包含了本变体的 `variant.h`**，
所以 `SX126X_DIO2_AS_RF_SWITCH` 是生效的，而非被静默忽略。

包含链：`SX126xInterface.cpp` → `#include "configuration.h"`（该文件第 3 行）
→ `configuration.h` 第 37 行 `#include "variant.h"` → 经
`-Ivariants/esp32s3/diy/tmesh-s3-e22` 解析到本变体。

`.d` 文件是预处理器处理完 `#include` 后写出的，属于编译过程的副产物，
**不是配置文件的自述**，所以它比读 `platformio.ini` 更可靠。

#### 验证二：功率宏的**数值**确实正确

只看"宏被定义了"不够，还要看它**展开成多少**。这里用了两道办法：

**a) 编译命令层**——确认开关被传进去了：

```powershell
# pio run -t compiledb 会生成 compile_commands.json
$j = Get-Content compile_commands.json -Raw | ConvertFrom-Json
($j | Where-Object { $_.file -like '*RadioInterface.cpp' }).command |
  Select-String -Pattern 'EBYTE_E22_900M30S|TMESH_LOW_POWER_TEST'
```

`EBYTE_E22_900M30S` 与 `TMESH_LOW_POWER_TEST` 都在。

**b) 数值层**——用**编译期断言**。这比读日志强，因为值不对就直接编译失败：

```c
// src/mesh/SX126xInterface.cpp 顶部 (所有头文件之后)
#if defined(TMESH_S3_E22)
#if defined(TMESH_LOW_POWER_TEST)
#if (TX_GAIN_LORA != 7) || (SX126X_MAX_POWER != 15)
#error "TMESH: 小功率模式预期 TX_GAIN_LORA=7 / SX126X_MAX_POWER=15。"
#endif
#else
#if (TX_GAIN_LORA != 7) || (SX126X_MAX_POWER != 22)
#error "TMESH: E22-900M30S 预期 TX_GAIN_LORA=7 / SX126X_MAX_POWER=22。"
#endif
#endif
#endif
```

实测结果（两种模式都编过）：

| 配置 | 断言结果 | 含义 |
| --- | --- | --- |
| 开 `TMESH_LOW_POWER_TEST` | ✅ 通过（要求 7/15） | 小功率模式生效，约 32 mW |
| 关 `TMESH_LOW_POWER_TEST` | ✅ 通过（要求 7/22） | 恢复上游实测表，约 29 dBm |

**这道断言还真的抓到过一个 bug**：一开始我把小功率覆盖写在 `variant.h` 里，
但 `variant.h` 是在 `configuration.h:37` 被包含的，而 `SX126X_MAX_POWER`
要到 `configuration.h:147` 才定义 —— 我的 `#undef`/`#define 15` 发生在它
还不存在的时候，随后被 `#define 22` 覆盖。断言立刻报错，暴露了这个顺序错误。
现在覆盖逻辑放在 `configuration.h` 中 E22 段**之后**。

> 教训：**`variant.h` 不是"万能注入点"。** 它被包含的位置（`configuration.h:37`）
> 早于该文件后续大量 `#define`。凡是依赖 `configuration.h` 后段定义的宏，
> 覆盖/校验都不能写在 `variant.h` 里。

---

## 5. 上板后必须验证的顺序

> ### ⚠️ 刷机前：如果你之前刷过 16MB 分区的构建
>
> 分区表从 `default_16MB.csv` 换成了 `default_8MB.csv`，**NVS 区的地址变了**。
> 直接原地更新的话，已保存的 Meshtastic 配置会从错误的位置读取（表现为配置
> 丢失、反复重启、或节点 ID 异常）。请先整片擦除：
>
> ```bash
> esptool.py --chip esp32s3 erase_flash
> # 然后刷 factory 镜像
> esptool.py --chip esp32s3 write_flash 0x0 tmesh-s3-e22-factory.bin
> ```
>
> 全新芯片、或用的是 N8R2 正确的 8MB 布局，则不需要这一步。

按这个顺序测，能最快定位问题：

1. **先只确认射频能起来**：串口日志里找
   `Set DIO2 as RF switch, result: 0`
   如果 result 非 0 或这行缺失 → `SX126X_DIO2_AS_RF_SWITCH` 没生效。
2. **确认 U4 跳线是短接的**（万用表蜂鸣档量 U4 两个焊盘）。
   开路则 TXEN 恒低，模块停在约 2 µA 软件关断，**收发全废**。
3. **TX 电流**：1 W 发射时整机电流应有明显跳升（约 +650 mA 量级）。
   若发射电流几乎不变 → 射频开关没切到发射路径。
4. **RX 灵敏度**：两颗设备对通，看 `rx_snr` / `rx_rssi` 是否正常。
5. **I2C 设备扫描**：确认 OLED / BMI160 / BMP280 是否都在总线上。
   ⚠️ OLED 由焊桥 `U15` 接在 +5 V，详见第 7 节。
6. **GPS**：确认 GPIO8/9 方向正确、PPS 有跳变。

---

## 6. 发射功率设置（这一节很重要）

### 6.1 Meshtastic 的功率机制

Meshtastic 有两层限制，**含义完全不同**，很容易搞混：

```c
// src/mesh/RadioInterface.cpp:1455  limitPower()
power = config.lora.tx_power;              // 用户请求的功率
if (power > region->powerLimit && !licensed) power = region->powerLimit;
power -= TX_GAIN_LORA;                     // ← 减去外部 PA 增益
if (power > loraMaxPower) power = loraMaxPower;   // ← loraMaxPower = SX126X_MAX_POWER
// 结果才传给 lora.setOutputPower()
```

| 宏 | 含义 | 谁在管 |
| --- | --- | --- |
| `TX_GAIN_LORA` | 外部 PA 的增益（dB）。会被**减掉** | 上游 `configuration.h` 的模块预设 |
| `SX126X_MAX_POWER` | **最终发射功率**的天花板。**不是**传给芯片的值 | 同上 |
| `NUM_PA_POINTS` | 增益表段数。单段固定增益 = 1；非线性 PA 用 22 段曲线 | 同上 |

`config.lora.tx_power` 默认是 **0**，会被解释成"用区域上限"（US = 30 dBm）。

### 6.2 上游已经内置了 E22-900M30S 的实测表

**不需要自己算，也不需要自定义映射表。** `src/configuration.h:143`：

```c
#ifdef EBYTE_E22_900M30S
// 10dB PA gain and 30dB rated output; based on measurements from
// https://github.com/S5NC/EBYTE_ESP32-S3/.../E22-900M30S power output testing.txt
#define TX_GAIN_LORA 7
#define SX126X_MAX_POWER 22
#endif
```

**所以正确做法就是加一个 build flag：`-D EBYTE_E22_900M30S`。**

上游有 6 个 variant 都在用这个开关，包括 `WashTastic`（正是这块板子描述里"致敬"的那个项目）。

### 6.2.1 ⚠️ 不同 E22 型号的芯片输入上限差 14 dB —— 换模块必读

**这是最容易烧硬件的坑。** 同为 E22-900M 系列，对"射频芯片该给多大功率"的要求完全不同：

| | **E22-900M30S**（本板） | **E22-900M33S** |
| --- | --- | --- |
| 最大发射功率 | 29.5 / **30.0** / 31 dBm | 32.5 / **33** / 33.5 dBm |
| **芯片输出上限** | **datasheet 无此限制** | ⚠️ **"The output power of the RF chip cannot be greater than 9dBm"** |
| 发射电流 | **650 mA** | **1200 mA** |
| 阻塞功率 | 10 dBm | 10 dBm |
| Meshtastic 预设 | `TX_GAIN_LORA 7` / `SX126X_MAX_POWER 22` | `TX_GAIN_LORA 25` / `SX126X_MAX_POWER 8` |

**芯片输出上限相差 14 dB。** 把 30S 的配置套到 33S 上会直接打坏内部 PA。

> **别被命名误导**：型号里的数字不是增益。`900M33S` 的 PA 增益高达 **25 dB** ——
> 正因为它增益太高，才必须把输入压到 9 dBm 以下；而 30S 增益只有约 7~10 dB，
> 所以能吃下芯片的 +22 dBm 满功率。
>
> 出处：
> - 33S: <https://www.cdebyte.com/products/E22-900M33S/1#Parameters>
>   （Maximum transmit power 一行，Remark 列写明了 9 dBm 限制）
> - 30S: `Datasheet/E22-900M30S_datasheet_C411294_*.pdf` 主要参数表
>   （最大发射功率 29.5/30.0/31 dBm，**无**芯片输入限制）

**代码里已加防呆**（`variant.h`）：同时定义 `EBYTE_E22_900M30S` 和
`EBYTE_E22_900M33S` 会直接 `#error` 编译失败。

**真要换 33S 时，三件事必须一起做**：

1. `platformio.ini` 改为 `-D EBYTE_E22_900M33S`（用它自己的 25/8 表）
2. **重新核算供电** —— 本板 MP2482 与 470 µF 储能是按 650 mA 设计的，
   33S 需要 1200 mA
3. **确认当地法规** —— 2 W 在多数地区需要许可，不能当 drop-in 替换

**另一个容易忽略的点**：`Blocking power 10 dBm` 对两块模块都一样。
近距离（<1 m）用大功率对射有烧接收前端的风险，出厂说明也提到"近距离使用烧毁概率较小"。


> ### ⚠️ 不加这个 flag 会怎样（这是个静默陷阱）
>
> `configuration.h:222/226` 有兜底默认值：
> ```c
> #ifndef NUM_PA_POINTS
> #define NUM_PA_POINTS 1
> #endif
> #ifndef TX_GAIN_LORA
> #define TX_GAIN_LORA 0     // ← 增益当 0
> #endif
> ```
> 于是 `TX_GAIN_LORA = 0`，`power` 不会被减去增益，`SX126X_MAX_POWER` 若是 22，
> 芯片就以 **22 dBm 直出**，再叠加 E22 内部 PA 的约 7 dB → 实际远超额定，
> 且**完全不受控**。整个编译过程不会给任何警告。
>
> 本 variant 已加编译期校验（见 6.4），漏了会直接编译失败。

### 6.3 首次上电：小功率模式

`platformio.ini` 里有一行默认打开的开关：

```ini
-D TMESH_LOW_POWER_TEST
```

它把 `SX126X_MAX_POWER` 改成 **15**，于是：

```
上限 15 dBm  →  芯片 15 − 7 = 8 dBm  →  模块输出约 15 dBm ≈ 32 mW
```

比 1 W 低约 **15 dB**，用来验证射频链路、电源、U4 跳线足够，也不容易烧东西或违规。
验证完之后把那行注释掉，就恢复到上游实测表：

```
区域上限 30 dBm (US)  →  芯片 30 − 7 = 23 → 夹到 22 dBm  →  模块约 29 dBm
```

**注意**：`limitPower()` 是"天花板"语义。你在 App / CLI 里把 `tx_power` 调低到 10 dBm，
小功率模式下最终就是 10 dBm（不会反而变高）。所以两种方式都能压低功率，
但 `-D TMESH_LOW_POWER_TEST` 的好处是**下限有保障**，不依赖 App 设置。

### 6.4 编译期防呆

`variant.h` 里有两道检查：

1. `#if defined(USE_SX1262) && !defined(EBYTE_E22_900M30S)` → `#error`
2. `src/mesh/SX126xInterface.cpp` 顶部（所有头文件之后）校验实际数值：
   - 小功率模式：`TX_GAIN_LORA == 7 && SX126X_MAX_POWER == 15`
   - 正常模式：`TX_GAIN_LORA == 7 && SX126X_MAX_POWER == 22`

> **位置很关键**：`variant.h` 是 `configuration.h:37` 包含的，而
> `TX_GAIN_LORA` / `NUM_PA_POINTS` 要到 `configuration.h:143/222` 才定义 ——
> 也就是说在 `variant.h` 里做数值校验时它们**还没展开**，会无条件报错。
> 所以数值校验必须放在 `.cpp` 里所有头文件之后。

### 6.5 其他可微调项

| 项 | 当前值 | 何时需要调整 |
| --- | --- | --- |
| `USE_SSD1306` vs `USE_SH1106` | SSD1306 | OLED 花屏 / 显示偏移半屏时对调 |
| `GPS_BAUDRATE` | 9600 | 若 ATGM336H 被改成 115200，需同步 |
| `SX126X_PA_RAMP_US` | 未定义（默认 200 µs） | 若频谱仪看到发射启停有杂散，可试 `0x05`（800 µs，同 T-Beam-1W） |

---

## 7. 对上游源码的改动（共 2 处，均为追加，未修改原有逻辑）

`variant.h` / `platformio.ini` 是"新增文件"，直接放进 `variants/` 即可。
但有两处需要动上游源码，因为要访问 `configuration.h` 后段才定义的宏。
两份改后文件的副本放在 `Review/meshtastic-variant/upstream-modified/`。

| 文件 | 改动 | 为什么必须在这里 |
| --- | --- | --- |
| `src/configuration.h` | 在 `EBYTE_E22_900M30S` 段**之后**追加小功率覆盖块（仅 `TMESH_S3_E22 && TMESH_LOW_POWER_TEST` 时生效） | `SX126X_MAX_POWER` 在本文件 147 行才定义，覆盖必须在其后 |
| `src/mesh/SX126xInterface.cpp` | 顶部（所有头文件之后）追加编译期断言块，`#if defined(TMESH_S3_E22)` 包裹 | 需要 `TX_GAIN_LORA` / `NUM_PA_POINTS` / `SX126X_MAX_POWER` 全部展开后 |

两处都用 `#if defined(TMESH_S3_E22)` 严格限定，**不影响其他任何板子**。

### 7.1 如果要向上游提 PR

不需要改上游也能工作 —— 直接定义 `SX126X_MAX_POWER` 会引发宏重定义，所以
正规做法是给上游提一个模块级的功率选择。可参考上游对
`NICERF_F30_HF`、`RAK13302` 的处理方式：在 `configuration.h` 里加一段
`#ifdef <你的板级宏>` 并给出实测功率曲线。

本板可以直接复用上游已有的 `EBYTE_E22_900M30S`，所以**只差小功率调试开关**这一项。

---

## 8. 硬件遗留问题（与固件无关，但会影响上板验证）

来自 `Review/HARDWARE-REVIEW.md`，上电前请先处理：

- **`U13` (OLED) 由焊桥 `U15` 接在 +5 V。** 多数 4-pin 0.96" OLED 无板载 LDO、标称 3.3 V。
  若它自带上拉且内部为 5 V 轨，会把 5 V 带上 I2C 总线，超规 GY-BMP280 的 IO 与 ESP32-S3 的 3.3 V GPIO。
  上电前实测一次；必要时把 `U15` 改到 3.3 V 一侧。

**已确认无需处理**（初版复核的误报，设计者指出后经复核成立）：

- `U11` (GY-BMP280) **由 BMI160 的 LDO 输出 3.3 V 供电**（`BMI_3V` → `U12` 焊桥 → `$1N126` → `U11.1`），
  在 BMP280 工作范围 1.71–3.6 V 之内，**合规**。
- `PD → DC` **是有意设计**（EasyEDA 源码丝印含 `"PD -> DC"`），不是倒灌缺陷。
- `U10` (PCF8563) 由 +5 V 供电，芯片范围 1.0–5.5 V，**合法**。
