/**
 * board_tmesh.h — T-mesh (ESP32-S3 + E22-900M30S) 板级引脚定义
 * ============================================================================
 * 板子:     T-mesh  (ESP32-S3-SX1262-Meshtatics-Station)
 * 设计者:   Tacoin  |  嘉立创EDA Pro
 * 依据:     PCB/Netlist_Schematic1_1_2026-10-14.tel
 *           PCB/Gerber_PCB1_1_2026-09-06.zip -> FlyingProbeTesting.json
 *           PCB/SCH_Schematic1_1_2026-10-14.pdf
 *
 * 所有引脚号均从网表 + Gerber 飞针数据双重核对，非推测。
 *
 * ============================================================================
 *  ★★★ 射频 TX/RX 切换 —— 本板最关键、最容易踩的一点 ★★★
 * ============================================================================
 *
 * 硬件拓扑:
 *
 *   E22 U2.8 (DIO2) ─┬─ U4 (2-pad 跳线, 默认短接) ──> U2.7 TXEN  (高有效)
 *                    └─ U3 (SN74LVC1G14 施密特反相器)
 *                            U3.4 (输出) ──────────> U2.6 RXEN  (高有效)
 *
 *   真值表 (U4 短接时):
 *     DIO2 = 1  ->  TXEN = 1, RXEN = 0  ->  发射
 *     DIO2 = 0  ->  TXEN = 0, RXEN = 1  ->  接收
 *
 * ★ 固件必须执行 SetDio2AsRfSwitchCtrl(1)，让 SX1262 把 DIO2 当射频开关
 *   控制脚来驱动。见下方 radio_configure_rf_switch()。
 *
 * ★ 如果漏了这一步，DIO2 不会被芯片驱动，TXEN/RXEN 静止不动，E22 会停在
 *   软件关断状态（约 2 uA）—— 现象是"完全收不到也发不出去"，很容易被误判
 *   成 SPI 或供电问题。
 *
 * ★ U4 必须实物短接。它开路时 TXEN 是孤立节点（网表里 RADIO_TXEN 只有
 *   U2.7 和 U4.2 两个点），TXEN 恒低 -> 射频开关永远切不到发射。
 *
 * ★ 本板没有额外的 MCU GPIO 用于控制 TX/RX，这是有意的设计：一个 DIO2
 *   信号配合一颗反相器就完成了互斥切换。不要去找 "TXEN_PIN"。
 *
 * ============================================================================
 *  ★ 电源与焊桥（已复核确认）★
 * ============================================================================
 *
 *   [确认] U11 (GY-BMP280) 由 BMI160 的 3.3V LDO 供电，不是 5V：
 *            U7.2 = BMI_3V (BMI160 模块 3V3 脚)
 *            U12 焊桥: 公共端 BMI_3V -> $1N126
 *            U11.1 (GY-BMP280 VCC) = $1N126
 *          BMP280 工作范围 1.71-3.6V，3.3V 完全合规。
 *
 *   [注意] U13 (OLED) 由焊桥 U15 接到 +5V ($1N132)。
 *          多数 4-pin 0.96" OLED 无板载 LDO，标称 3.3V。
 *          若其自带 I2C 上拉且内部为 5V 轨，会把 5V 带上总线，
 *          超规 GY-BMP280 的 IO 和 ESP32-S3 的 3.3V GPIO。
 *          上电前请实测；必要时把 U15 改到 3.3V 一侧
 *          (ESP_3V3 = U1.36 目前只带一颗反相器，余量充足)。
 *
 *   [注意] U10 (PCF8563) 由 +5V 供电 —— 芯片工作范围 1.0-5.5V，合法。
 *
 *   [确认] PD -> DC 是有意设计，不是缺陷。
 *          EasyEDA 源码丝印含 "PD -> DC"。
 *          $1N26 = {PD1.2, D1.1}，VCC = {D1.2, DC1.1, H2.1, U16.1}
 *

#ifndef BOARD_TMESH_H
#define BOARD_TMESH_H

/* ==========================================================================
 * 1. LoRa 射频 —— Ebyte E22-900M30S (U2, SX1262 + PA + LNA, 1W)
 *    引脚对照: 网表 U2.13..U2.19
 * ========================================================================== */

/* SPI 总线 */
#define LORA_SCK        12   /* U2.18  <- U1.2  (GPIO12) */
#define LORA_MISO       13   /* U2.16  -> U1.4  (GPIO13) */
#define LORA_MOSI       11   /* U2.17  <- U1.1  (GPIO11) */
#define LORA_NSS        10   /* U2.19  <- U1.35 (GPIO10), 片选 */

/* 控制与状态 */
#define LORA_RESET       5   /* U2.15  <- U1.27 (GPIO5),  低有效 */
#define LORA_BUSY        4   /* U2.14  -> U1.26 (GPIO4)         */
#define LORA_DIO1        1   /* U2.13  -> U1.24 (GPIO1),  中断 */

/* DIO2 (U2.8) —— 不占用 MCU GPIO！
 * 它接 U3 反相器输入和 U4 跳线，由 SX1262 内部 RF switch 控制器驱动。
 * 固件里不要定义 gpio 给它，只需在初始化时配置功能。 */
#define LORA_DIO2_IS_RF_SWITCH   1   /* 恒为 1，仅作自文档用 */

/* 射频开关控制是"做"出来的，不是"接"出来的 —— 没有对应 GPIO */
/* #define LORA_TXEN_PIN   —— 不存在，由 DIO2 经 U4 跳线驱动 */
/* #define LORA_RXEN_PIN   —— 不存在，由 DIO2 经 U3 反相后驱动 */

/* E22 供电: U2.9 / U2.10 = +5V  (范围 2.5-5.5V, 本板用 5V) */
#define LORA_ANTENNA_NOTE \
    "ANT = U2.21, 邮票孔 50ohm。需外接 868/915MHz 天线, 1W 时约 650mA 峰值"

/* ==========================================================================
 * 2. GNSS —— ATGM336H 5N (U6, 5-pin 模块)
 * ========================================================================== */
#define GPS_PPS          6   /* U6.5 -> U1.29 (GPIO6)             */
#define GPS_TX           8   /* U6.3 -> U1.32 (GPIO8), ESP32 收  */
#define GPS_RX           9   /* U6.4 <- U1.34 (GPIO9), ESP32 发  */

/* 注意: U6 只用了 5 个脚 (+5V/GND/TX/RX/PPS)，模块上【没有 WAKEUP 脚】。
 * 网表里 GPS_WAKEUP 这个网络实际只有 GPIO7 和 H1 两个点，
 * 即 GPIO7 引到一个 2.54mm 排针，【并没有连到 GNSS 模块】。 */
#define GPS_WAKEUP       7   /* -> H1 1P 排针, 备用 IO, 非真实 GNSS 控制线 */
#define GPS_WAKEUP_IS_REAL_GNSS_CONTROL  0   /* 0 = 不连 GNSS, 仅排针 */

/* ==========================================================================
 * 3. I2C 总线 —— 四颗从设备共用 GPIO17/18
 *    U7 (GY-BMI160) / U10 (PCF8563) / U11 (GY-BMP280) / U13 (OLED 0.96")
 * ========================================================================== */
#define I2C_SDA         17   /* U7.5 / U10.3 / U11.4 / U13.4 */
#define I2C_SCL         18   /* U7.4 / U10.4 / U11.3 / U13.3 */

/* 从设备地址(参考值, 上电后请自行扫描确认) */
#define I2C_ADDR_OLED       0x3C  /* SSD1306 常见值, 模块可能为 0x3D */
#define I2C_ADDR_PCF8563    0x51  /* PCF8563 固定 7-bit 地址 */
#define I2C_ADDR_BMP280     0x76  /* SDO 接地 -> 0x76, 接 VDDIO -> 0x77 */
#define I2C_ADDR_BMI160     0x68  /* SDO 接地 -> 0x68, 接 VDDIO -> 0x69 */

/* ==========================================================================
 * 4. 中断脚
 * ========================================================================== */
#define RTC_INT         14   /* U10.6 -> U1.5  (GPIO14), PCF8563 中断 */
#define IMU_INT         33   /* U7.10 -> U1.11 (GPIO33), BMI160 INT1  */

/* ==========================================================================
 * 5. 重要事实 —— BMI160 走的是 I2C, 不是 SPI
 * ==========================================================================
 * 网表确认: U7.4 = GPIO18 (SCL), U7.5 = GPIO17 (SDA), U7.10 = GPIO33 (INT1)
 * README 里写的 "IMU SPI MOSI/SCK/MISO = GPIO35/36/37" 与实际不符:
 * GPIO35/36/37 在网表里都是孤立单点网络, 没有接任何器件。
 * BMI160 在 GY-BMI160 模块上通过 CSB 脚选择 I2C/SPI, 本板按 I2C 用。
 */
#define IMU_USES_I2C             1
#define IMU_USES_SPI             0   /* 硬件上没接 SPI! */

/* ==========================================================================
 * 6. 空引脚 —— 网表里为孤立单点网络, 可作他用
 *    注意: GPIO35/36/37 常被误当成 IMU SPI, 实际是空的
 * ========================================================================== */
/* 可用: GPIO2, GPIO15, GPIO16, GPIO34, GPIO35, GPIO36, GPIO37,
 *       GPIO38, GPIO39, GPIO40, GPIO41, GPIO42
 * 已用: 见上
 * Pico 不引出: GPIO0, GPIO3, GPIO19, GPIO20, GPIO21-26, GPIO43-48
 */
#define PIN_FREE_2       2
#define PIN_FREE_15     15
#define PIN_FREE_16     16
#define PIN_FREE_34     34
#define PIN_FREE_35     35   /* 空! 不是 IMU MOSI */
#define PIN_FREE_36     36   /* 空! 不是 IMU SCK  */
#define PIN_FREE_37     37   /* 空! 不是 IMU MISO */
#define PIN_FREE_38     38
#define PIN_FREE_39     39
#define PIN_FREE_40     40
#define PIN_FREE_41     41
#define PIN_FREE_42     42

/* ==========================================================================
 * 7. 电源事实
 * ==========================================================================
 *   12V 输入: DC-005-A200 (DC1.1) 或 PDC004 PD 诱骗模块 (PD1.2)
 *             两路在 $1N26 节点直接并联, 经 SS54 (D1) 后成为 VCC
 *   VCC -> U16 (MP2482 降压模块 U16.1) -> +5V (U16.4)
 *   +5V 负载: U2 (E22), U6 (GNSS), U7 (BMI160), U10 (RTC), U13 (OLED),
 *             U1.39 (Pico VBUS), 以及 U17 470uF / U5 22uF / U18-20 10uF
 *   ESP_3V3 (= U1.36, Pico 的 3V3 OUT) 负载: 仅 U3 (反相器 VCC)
 *   BMI_3V (经焊桥 U12 从 +5V 取) 负载: 仅 U7.2 (GY-BMI160 的 3V3 脚)
 *
 *   本板【没有电池路径】: U1 的 VSYS (pin37/38) 未使用。
 *   发射峰值电流约 650mA, 全部由 MP2482 的 5V 输出承担。
 */
#define BOARD_HAS_BATTERY        0
#define BOARD_INPUT_VOLTAGE_MV   12000
#define BOARD_RAIL_RADIO_MV      5000
#define BOARD_RADIO_PEAK_MA      650

/* ==========================================================================
 * 8. 射频初始化辅助
 * ========================================================================== */

/**
 * 让 SX1262 用 DIO2 驱动外部射频开关。
 *
 * 这是本板【必需】的一步。原理:
 *   DIO2 在发射时输出高, 经 U4 直接拉高 E22 的 TXEN;
 *   同一个 DIO2 经 U3 反相后输出低, 拉低 RXEN。
 *   接收时反之。因此一个信号即可完成互斥切换。
 *
 * RadioLib:
 *     radio.setDio2AsRfSwitch(true);
 *
 * LoRaMac-node / 寄存器级:
 *     写 SetDio2AsRfSwitchCtrl 命令 (opcode 0x9D), 参数 0x01。
 *
 * 注意: 必须在进入 RX/TX 之前、且在 SetStandby 之后配置, 否则第一次
 *       收发可能用错开关方向。
 */
#define BOARD_REQUIRES_DIO2_RF_SWITCH   1

#endif /* BOARD_TMESH_H */
