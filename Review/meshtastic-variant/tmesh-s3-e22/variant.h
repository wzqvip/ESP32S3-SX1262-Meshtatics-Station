// ============================================================================
// T-mesh — Meshtastic board variant
// Waveshare ESP32-S3-Pico (ESP32-S3 N8R2: 8MB flash / 2MB PSRAM)
// + Ebyte E22-900M33S (SX1262 + PA + LNA, 2W)
//   ★ 是 33S, 不是 30S: 芯片输入上限 9 dBm, 发射电流约 1200 mA。
//     引脚兼容的 30S 功率表相反, 混用会打坏 PA —— 见下方防呆断言。
// ============================================================================
// 所有引脚均从 Netlist_Schematic1_1_2026-10-14.tel 与 Gerber 内嵌
// FlyingProbeTesting.json (386 焊盘网络表) 双重核对, 非推测。
//
// ╔══════════════════════════════════════════════════════════════════════════╗
// ║  ★★★ 射频 TX/RX 切换 —— 本 variant 最容易被改错的地方 ★★★             ║
// ╚══════════════════════════════════════════════════════════════════════════╝
//
// 本板硬件拓扑 (已实物确认 U4 跳线默认短接):
//
//     E22 U2.8 DIO2 ─┬── U4 (2 焊盘跳线) ──────────────> E22 U2.7 TXEN (高有效)
//                    └── U3 (SN74LVC1G14 施密特反相器) ─> E22 U2.6 RXEN (高有效)
//
//   真值表:
//     DIO2 = 1  ->  TXEN = 1, RXEN = 0  ->  发射路径
//     DIO2 = 0  ->  TXEN = 0, RXEN = 1  ->  接收路径
//
// ★ 因此必须使用 SX126X_DIO2_AS_RF_SWITCH。
//   它让 Meshtastic 调用 lora.setDio2AsRfSwitch(true),
//   由 SX1262 芯片在 TX/RX 之间自动翻转 DIO2, 硬件反相器再生成 RXEN。
//
// ★★ 绝对【不要】定义 SX126X_TXEN 或 SX126X_RXEN ★★
//   本板这两个信号根本没有连到 MCU GPIO。若定义了, RadioLib 的
//   setRfSwitchPins() 会去操作不存在的 GPIO, 与芯片自身的 DIO2 驱动打架,
//   结果可能是发射功率异常或完全无法收发。
//
//   注意区别: 同目录下的 variants/esp32s3/diy/t-energy-s3_e22 也是 E22 模块,
//   但它把 TXEN/RXEN 接到了 MCU (SX126X_TXEN 10 / SX126X_RXEN 12)。
//   那是另一种板子, 【不能】照抄到本板。
//
// ============================================================================

// ---------------------------------------------------------------------------
// LoRa 射频 — Ebyte E22-900M33S (U2)
// 网表: U2.16..U2.19 (SPI), U2.13 (DIO1), U2.14 (BUSY), U2.15 (NRST)
// ---------------------------------------------------------------------------
#define USE_SX1262
// 注意: E22-900Mxx 是 SX1262 内核 (900MHz)。E22-400Mxx 才是 SX1268 (433MHz)。
//       本板射频匹配为 900MHz, 若换 400M 模块需要同时改硬件匹配和频段。
// #define USE_SX1268

#define LORA_CS 10    // U2.19 NSS  <- U1.35 (GPIO10)
#define LORA_SCK 12   // U2.18 SCK  <- U1.2  (GPIO12)
#define LORA_MOSI 11  // U2.17 MOSI <- U1.1  (GPIO11)
#define LORA_MISO 13  // U2.16 MISO -> U1.4  (GPIO13)
#define LORA_RESET 5  // U2.15 NRST <- U1.27 (GPIO5),  低有效
#define LORA_BUSY 4   // U2.14 BUSY -> U1.26 (GPIO4)
#define LORA_DIO1 1   // U2.13 DIO1 -> U1.24 (GPIO1),  中断

#ifdef USE_SX1262
#define SX126X_CS LORA_CS
#define SX126X_SCK LORA_SCK
#define SX126X_MOSI LORA_MOSI
#define SX126X_MISO LORA_MISO
#define SX126X_RESET LORA_RESET
#define SX126X_BUSY LORA_BUSY
#define SX126X_DIO1 LORA_DIO1

// ★★★ 本板核心配置: DIO2 驱动射频开关 (TXEN 直连 + RXEN 经反相器) ★★★
#define SX126X_DIO2_AS_RF_SWITCH

// 这里【故意】没有 SX126X_TXEN / SX126X_RXEN —— 见文件头说明。

// E22 系列内部用 DIO3 给 32MHz TCXO 供电, 参考电压 1.8V。
// (与 variants/esp32s3/diy/t-energy-s3_e22 的 E22 配置一致; 见 E22 datasheet
//  "内部使用 DIO3 为 32MHz TCXO 晶振供电")
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

// ===========================================================================
//  ★★★ 发射功率设置 ★★★   这是本板第二个容易搞错的地方
// ===========================================================================
//
// 上游在 src/configuration.h:143 已经为这块模块内置了实测出来的功率映射表:
//
//     #ifdef EBYTE_E22_900M30S
//     // 10dB PA gain and 30dB rated output; based on measurements from
//     // https://github.com/S5NC/EBYTE_ESP32-S3/.../E22-900M30S power output testing.txt
//     #define TX_GAIN_LORA 7
//     #define SX126X_MAX_POWER 22
//     #endif
//
// 所以 platformio.ini 里必须带模块宏, 由上游来决定这两个值。
// 下面这段做一致性校验 —— 漏定义或选错模块都直接编译失败。
//
// ★ 本板装的是 E22-900M33S (2W), 但它和 30S 是同一个封装/引脚,
//   只看板子外观分不出来, 所以这里用编译期断言把两者钉死。
#if defined(USE_SX1262)
#if !defined(EBYTE_E22_900M33S) && !defined(EBYTE_E22_900M30S)
#error "必须定义模块宏: 本板装 E22-900M33S, 应在 platformio.ini 里加 -D EBYTE_E22_900M33S。否则 TX_GAIN_LORA 会落到默认 0, 芯片以 22dBm 直出, 远超 33S 的 9dBm 输入上限。"
#endif
#if defined(EBYTE_E22_900M33S) && defined(EBYTE_E22_900M30S)
#error "EBYTE_E22_900M33S 与 EBYTE_E22_900M30S 不能同时定义: 两者功率表相反 (25/8 vs 7/22), 会烧 PA。"
#endif
#endif // USE_SX1262

// --- 数值含义 (给以后改的人) ------------------------------------------------
//
// 本板【没有】SX1262 功率优化表 —— EBYTE_E22_900M30S 那段只定义了
// TX_GAIN_LORA / SX126X_MAX_POWER, 没定义 SX126X_NO_POWER_OPTIMIZATION_TABLE。
// 所以 RadioLib 用内置功率表, SX1262 输出上限就是 +22dBm。
//
// TX_GAIN_LORA = 7 : 模块 PA 的实测增益。RadioInterface::limitPower() 会做
//                    "芯片功率 = 请求功率 − 增益"。默认 tx_power=0 会被解析成
//                    区域上限(US = 30dBm), 30 − 7 = 23dBm, 再被
//                    SX126X_MAX_POWER=22 夹到 22dBm → 模块输出约 29dBm。
//
// SX126X_MAX_POWER = 22 : limitPower() 里"最终功率"的天花板值()。
//                    RadioInterface.cpp:1500  `if (power > loraMaxPower) power = loraMaxPower;`
//                    它【不是】直接传给 setOutputPower 的芯片功率 —— 芯片功率还要再减 TX_GAIN_LORA。
//
// --- 首次上电: 小功率模式 ---------------------------------------------------
//
// platformio.ini 里的 -D TMESH_LOW_POWER_TEST 把最终功率天花板压到很低,
// 便于先验证射频链路、电源和 U4 跳线, 不用一上来就跑 1W。
//
// 数值: 上限 15dBm → 芯片 15 − 7 = 8dBm → 模块输出约 15dBm ≈ 32mW
//
// ★ 具体的 #undef/#define 覆盖【不在本文件做】。
//   本文件是在 configuration.h:37 被包含的, 而 SX126X_MAX_POWER 要到
//   configuration.h:147 (EBYTE_E22_900M30S 段内) 才被定义。在这里覆盖会
//   发生在它还不存在的时候, 随后被那个 #define 22 覆盖掉 —— 等于无效。
//   覆盖逻辑放在 configuration.h 中 E22 段之后, 见那里的注释。
//
// 校验放在 src/mesh/SX126xInterface.cpp 顶部 (所有头文件之后)。

#endif /* USE_SX1262 */

// ---------------------------------------------------------------------------
// I2C 总线 — 四颗从设备共用 GPIO17/18
//   U7 (GY-BMI160) / U10 (PCF8563) / U11 (GY-BMP280) / U13 (OLED 0.96")
// ---------------------------------------------------------------------------
#define I2C_SDA 17   // U7.5 / U10.3 / U11.4 / U13.4
#define I2C_SCL 18   // U7.4 / U10.4 / U11.3 / U13.3

// ---------------------------------------------------------------------------
// RTC — PCF8563 (U10, 地址 0x51)
// ---------------------------------------------------------------------------
// ★ 必须定义这个宏, 否则 I2C 扫描不会把 0x51 认成 RTC:
//     src/detect/ScanI2CTwoWire.cpp:412
//         #ifdef PCF8563_RTC
//             SCAN_SIMPLE_CASE(PCF8563_RTC, RTC_PCF8563, "PCF8563", addr)
//         #endif
//   不定义时的现象: 自检/扫描能确认器件在 0x51 应答, 但 Meshtastic 报
//   "RTC: unknown" 而不会启用它 (GPS 时间也就没有后备电池保护)。
//   硬件自检已用芯片级方式确认: PCF8563 在 0x51 正常应答。
//   参考上游: tbeam-s3-core / t-watch-s3 / m5stack_coreink 等都用 0x51。
//   定义后 configuration.h:41-43 会映射成:
//       #define PCF_RTC_ADDRESS PCF8563_RTC
//       #define PCF_RTC_CHIP PCF8xRTC::PCF8563
#define PCF8563_RTC 0x51
#define PIN_RTC_INT 14   // U10.6 PCF8563 中断 -> GPIO14
                         // (正确宏名是 PIN_RTC_INT; 上游 nano-g2-ultra / t-echo 用法一致)

// ---------------------------------------------------------------------------
// 加速度计 — GY-BMI160 (U7, I2C 0x68, CHIP_ID 0xD1)
// ---------------------------------------------------------------------------
// ★ 上游原本【没有】BMI160 驱动 (只有 BMI270 / BMX160 / BMM150 等)。
//   本仓库为这块板子补了一份: src/motion/BMI160Sensor.{h,cpp}
//   设计上与 BMI270Sensor 对齐 (纯 I2C 寄存器访问, 不依赖外部库),
//   但 BMI160 不需要 BMI270 的 8KB 配置块, 只需两条模式寄存器即可工作。
//
//   需要配套的上游改动 (都已在本地完成, 副本见
//   Review/meshtastic-variant/upstream-modified/):
//     1) src/detect/ScanI2C.h        -> 枚举里加 BMI160
//     2) src/detect/ScanI2C.cpp      -> firstAccelerometer() 列表里加 BMI160
//     3) src/detect/ScanI2CTwoWire.cpp -> 0x68/0x69 处按 CHIP_ID 0xD1 识别
//     4) src/motion/AccelerometerThread.h -> include + case 分支
//     5) 本文件: #define HAS_BMI160
//
//   硬件自检已用芯片级方式确认这颗芯片正常 (读 CHIP_ID = 0xD1)。
//
//   注意: BMI160 的 0x68 与 MPU6050 / BMI270 / LSM6DS3 冲突, 靠 0x00 寄存器
//   的固定值 0xD1 唯一区分 (MPU6050 是 0x75=0x68, BMI270 是 0x00=0x24,
//   BMX160 是 0x00=0xD8)。
#define HAS_BMI160

// ---------------------------------------------------------------------------
// 显示 — 0.96" I2C OLED (U13), 复用上面的 I2C 总线
// ---------------------------------------------------------------------------
// 本板 OLED 只用了 4 根线: +5V / GND / SDA / SCL (网表 U13.1..U13.4)
// 驱动芯片常见两种: SSD1306 (多数) 或 SH1106 (部分模块)。
// 如果屏幕花屏/偏移, 把下面两行对调即可。
#define HAS_SCREEN 1
#define USE_SSD1306
// #define USE_SH1106
// 不需要定义屏幕尺寸: SharedUIDisplay.h 里 SCREEN_WIDTH/SCREEN_HEIGHT
// 直接取自 display->getWidth()/getHeight(), 由驱动自己给出 128x64。

// ---------------------------------------------------------------------------
// GNSS — ATGM336H 5N (U6), 5-pin 模块
// 网表: U6.1=+5V  U6.2=GND  U6.3=TX  U6.4=RX  U6.5=PPS
// ---------------------------------------------------------------------------
#define HAS_GPS 1

// ★ 宏名指的是【ESP32 这一侧】的收发方向, 不是模块侧的引脚名。
//   上游 GPS.cpp 的用法是:
//       _rx_gpio = GPS_RX_PIN;   // ESP32 从这个脚【接收】
//       _tx_gpio = GPS_TX_PIN;   // ESP32 从这个脚【发送】
//   GPS.cpp 还会打印 "Use GPIOx for GPS RX" 印证的正是这一点。
//
//   接线 (来自飞针数据, 并与 GPS 诊断固件实测一致):
//       U6.3 = 模块的 TX ──> GPIO8   => GPIO8 是 ESP32 的 RX
//       U6.4 = 模块的 RX <── GPIO9   => GPIO9 是 ESP32 的 TX
//
//   ⚠️ 这里曾经写反过 (GPS_TX_PIN 8 / GPS_RX_PIN 9), 后果是 UART 从 GPIO9
//      读数据、一条 NMEA 都收不到 -> 自动探测全部失败 -> "No GNSS Module"。
//      实测判定方法见 gps-diag/ 工程: 只有 RX=8/TX=9 能收到 51 条 NMEA。
#define GPS_RX_PIN 8    // ESP32 接收 <- U6.3 (模块 TX)
#define GPS_TX_PIN 9    // ESP32 发送 -> U6.4 (模块 RX)
#define PIN_GPS_PPS 6   // U6.5 PPS -> GPIO6  (正确的宏名是 PIN_GPS_PPS)
#define GPS_BAUDRATE 9600  // ATGM336H 出厂默认 9600

// 注意: 网表里的 "GPS_WAKEUP" 网络实际只有 GPIO7 和 H1 (一个 2.54mm 排针)
// 两个点, 【并未连到 GNSS 模块】。U6 只用了 5 个脚, 模块上没有 WAKEUP 引脚。
// 所以这里【不】定义 GPS_WAKEUP_PIN。GPIO7 是一个引到排针的备用 IO。

// ---------------------------------------------------------------------------
// 状态指示灯 — WS2812 RGB (GPIO21)
// ---------------------------------------------------------------------------
// 设计者确认: GPIO21 上有一颗 WS2812 RGB LED。
// 注意: GPIO21 在网表里是【孤立单点网络】(NET_未布线), 因为它直接挂在
// ESP32-S3-Pico 模块自身的 GPIO21 上, 不在载板走线里 —— 所以从网表
// 看不出它接了什么, 只有实物/设计者知道。
#define HAS_NEOPIXEL
#define NEOPIXEL_COUNT 1
#define NEOPIXEL_DATA 21
#define NEOPIXEL_TYPE (NEO_GRB + NEO_KHZ800)

// ---------------------------------------------------------------------------
// 用户按钮 — Pico 模块上的 BOOT 键 (GPIO0)
// ---------------------------------------------------------------------------
// 原理图确认: K1 = BOOT, 接 GPIO0; K2 = RESET, 接 CHIP_PU(EN)。
// 参考上游官方 variants/esp32s3/esp32-s3-pico/variant.h:
//     #define BUTTON_PIN 0            // This is the BOOT button
//     #define BUTTON_NEED_PULLUP
//
// ★ 为什么必须定义 (而不是"本板没有按钮"):
//   - 不定义 -> input/InputBroker.cpp 的按钮代码整段被编译掉,
//     BOOT 键按下去毫无反应。
//   - 而且 graphics/draw/MenuHandler.cpp:242-248 的开箱欢迎横幅有三种文案,
//     取决于 HAS_TFT / BUTTON_PIN 是否定义:
//         #if HAS_TFT
//             "Swipe to navigate and long press to select..."
//         #elif defined(BUTTON_PIN)
//             "Click to navigate and long press to select or open a menu."   ← 我们要的
//         #else
//             "Use the Select button to open menus..."                        ← 之前落的这支
//     所以不定义 BUTTON_PIN 时, 屏幕会提示用户去按一个根本不存在的
//     "Select 按钮" —— 这正是设计者遇到的现象。
//
// ★ 单按钮时的实际映射 (src/input/InputBroker.cpp, 有 screen 的分支):
//     singlePress   = INPUT_BROKER_USER_PRESS  -> 单击 = 切换界面帧
//     longPress     = INPUT_BROKER_SELECT      -> 长按 500ms = 打开菜单 / 选择
//     longPressTime = 500
//   与横幅文案 "Click to navigate and long press to select" 完全一致。
//
//   (注: 无 screen 的分支是 longPress=NONE / longLongPress=SHUTDOWN,
//    那种配置下长按不做事、超长按是关机, 横幅文案对不上 —— 我们不走那支。)
//
// GPIO0 是 ESP32-S3 的 strapping 脚 (与 BOOT 功能共用):
//   - 需要 BUTTON_NEED_PULLUP 打开内部上拉, 空闲时保持高
//   - 上电瞬间按住会被当成"进入下载模式", 正常使用不受影响
#define BUTTON_PIN 0
#define BUTTON_NEED_PULLUP
// 本板只有一个按键, 不定义 ALT_BUTTON_PIN / CANCEL_BUTTON_PIN

// ---------------------------------------------------------------------------
// 本板没有的东西 (故意不定义, 避免 Meshtastic 去操作不存在的硬件)
// ---------------------------------------------------------------------------
// 无电池: U1 的 VSYS (pin37/38) 未使用, 全板 12V 供电。
//   -> 不定义 BATTERY_PIN / ADC_CHANNEL / ADC_MULTIPLIER
// 无蜂鸣器 / 无 SD 卡 / 无风扇。

// ---------------------------------------------------------------------------
// 硬件注意事项 (详见 Review/HARDWARE-REVIEW.md)
// ---------------------------------------------------------------------------
// [确认] U11 (GY-BMP280) 由 BMI160 的 3.3V LDO 供电, 不是 5V:
//          U7.2 = BMI_3V (GY-BMI160 的 3V3 脚)
//          U12 焊桥: 公共端 BMI_3V -> $1N126
//          U11.1 (GY-BMP280 VCC) = $1N126
//        BMP280 工作范围 1.71-3.6V, 3.3V 合规。
// [确认] PD -> DC 是有意设计 (EasyEDA 源码丝印含 "PD -> DC"), 不是倒灌缺陷。
// [确认] U10 (PCF8563) 由 +5V 供电 —— 芯片范围 1.0-5.5V, 合法。
// [注意] U13 (OLED) 由焊桥 U15 接到 +5V。多数 4-pin 0.96" OLED 无板载 LDO,
//        标称 3.3V。若其自带上拉且内部为 5V 轨, 会把 5V 带上 I2C 总线。
//        上电前实测; 必要时把 U15 改到 3.3V 一侧 (ESP_3V3 目前只带反相器)。
//
// 【本板没有电池/power-path】U1 的 VSYS 未使用。固件若报无电池或低电量属正常。
