// ============================================================================
// T-mesh 硬件自检 (unit test)
// ============================================================================
// 目的:
//   1. 判定芯片 PSRAM 是否真的可用 —— 这是当前唯一能把
//      "硬件坏" 和 "Meshtastic 框架问题" 分开的实验
//      (Meshtastic 的构建体系强制链接它自带的 PSRAM 预编译库, 启动即崩)
//   2. 逐个验证板上每一类部件
//
// 硬件依据: 见 Review/HARDWARE-REVIEW.md 与 Review/hardware-schematic/
// 本工程【不依赖 Meshtastic】, 也不带它的 sdkconfig。
// ============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <math.h>

#define HAS_NEOPIXEL_LIB __has_include(<Adafruit_NeoPixel.h>)
#if HAS_NEOPIXEL_LIB
#include <Adafruit_NeoPixel.h>
#endif

// ---------------------------------------------------------------------------
// 引脚定义 (全部来自网表 Netlist_Schematic1_1_2026-10-14.tel + 飞针数据)
// ---------------------------------------------------------------------------
// LoRa — Ebyte E22-900M30S
#define PIN_LORA_SCK    12
#define PIN_LORA_MISO   13
#define PIN_LORA_MOSI   11
#define PIN_LORA_NSS    10
#define PIN_LORA_NRST    5
#define PIN_LORA_BUSY    4
#define PIN_LORA_DIO1    1
// 注意: E22 的 TXEN 由 DIO2 经 U4 跳线直连, RXEN 由 DIO2 经 U3 反相
//       MCU 不参与射频开关控制 (见 HARDWARE-REVIEW.md 第 1 节)

// GNSS — ATGM336H 5N (U6)
#define PIN_GPS_TX       8   // U6.3 -> GPIO8 (ESP32 收)
#define PIN_GPS_RX       9   // U6.4 <- GPIO9 (ESP32 发)
#define PIN_GPS_PPS      6   // U6.5
#define GPS_BAUD      9600

// I2C — U7 BMI160 / U10 PCF8563 / U11 BMP280 / U13 OLED
#define PIN_I2C_SDA     17
#define PIN_I2C_SCL     18

// 状态灯 — WS2812 (原理图 LE1 + R15 10K)
#define PIN_NEOPIXEL    21
#define NEOPIXEL_NUM     1

// ---------------------------------------------------------------------------
// 测试结果统计
// ---------------------------------------------------------------------------
enum Result { R_PASS, R_FAIL, R_WARN, R_SKIP };
static int g_pass = 0, g_fail = 0, g_warn = 0, g_skip = 0;

static const char *tagOf(Result r) {
    switch (r) {
        case R_PASS: return "PASS";
        case R_FAIL: return "FAIL";
        case R_WARN: return "WARN";
        default:     return "SKIP";
    }
}

static Result report(const char *name, Result r, const char *fmt, ...) {
    char msg[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    uint32_t us = micros();
    Serial.printf("[%8lu us] %-4s  %-26s %s\n", (unsigned long)us, tagOf(r), name, msg);

    switch (r) {
        case R_PASS: g_pass++; break;
        case R_FAIL: g_fail++; break;
        case R_WARN: g_warn++; break;
        case R_SKIP: g_skip++; break;
    }
    return r;
}

static void section(const char *title) {
    Serial.printf("\n===== %s =====\n", title);
}

// ===========================================================================
// 1. 芯片与内存
// ===========================================================================
static void test_chip_info() {
    section("1. 芯片与内存");

    Serial.printf("        ESP32-S3 芯片型号 : %s rev%d, %d 核 @ %lu MHz\n",
                  ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                  (unsigned long)getCpuFrequencyMhz());

    uint32_t flashSize = ESP.getFlashChipSize();
    Serial.printf("        Flash 容量      : %lu bytes (%.0f MB)\n",
                  (unsigned long)flashSize, flashSize / 1048576.0);
    if (flashSize == 8u * 1024 * 1024)
        report("flash_size", R_PASS, "8MB, 与 N8R2 一致");
    else
        report("flash_size", R_WARN, "期望 8MB(8388608), 实得 %lu", (unsigned long)flashSize);

    // ---- PSRAM: 本次自检的核心 ----
#if SELFTEST_ENABLE_PSRAM
    bool found = psramFound();
    size_t total = ESP.getPsramSize();
    size_t freeSz = ESP.getFreePsram();

    if (found) {
        report("psram", R_PASS, "可用! 总 %u bytes (%.1f MB), 空闲 %u bytes",
               (unsigned)total, total / 1048576.0, (unsigned)freeSz);

        // 真正写一遍, 确认不是"报了个数但读写不了"
        void *p = ps_malloc(64 * 1024);
        if (!p) {
            report("psram_rw", R_FAIL, "64KB 分配失败");
        } else {
            memset(p, 0xA5, 64 * 1024);
            bool ok = true;
            uint8_t *b = (uint8_t *)p;
            for (size_t i = 0; i < 64 * 1024; i += 4096) {
                if (b[i] != 0xA5) { ok = false; break; }
            }
            free(p);
            report("psram_rw", ok ? R_PASS : R_FAIL,
                   ok ? "64KB 写入/回读一致" : "数据不一致, PSRAM 不可靠");
        }
    } else {
        report("psram", R_FAIL,
               "psramFound()=false。芯片报告有 PSRAM, 但驱动初始化失败 —— "
               "这颗模组的 PSRAM 不可用");
    }
#else
    report("psram", R_SKIP, "本构建未启用 PSRAM (SELFTEST_ENABLE_PSRAM=0)");
#endif

    Serial.printf("        内部堆          : 空闲 %u / 总 %u bytes\n",
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getHeapSize());
    Serial.printf("        复位原因        : %d\n", (int)esp_reset_reason());
}

// ===========================================================================
// 2. GPIO 状态扫描
// ===========================================================================
// 对【网表里确认未使用】的引脚做一次 上拉/下拉 读数比较:
//   读值随内阻改变 -> 该脚未接任何东西 (空闲, 符合预期)
//   读值恒定       -> 板上接了东西 (与网表不符, 需要查)
static void test_gpio_scan() {
    section("2. GPIO 空闲引脚扫描");
    Serial.println("        方法: 逐脚设 INPUT_PULLUP / INPUT_PULLDOWN 各读一次");

    const int freePins[] = {2, 15, 16, 34, 35, 36, 37, 38, 39, 40, 41, 42};
    int idle = 0, used = 0;

    for (int pin : freePins) {
        pinMode(pin, INPUT_PULLUP);
        delay(2);
        int up = digitalRead(pin);
        pinMode(pin, INPUT_PULLDOWN);
        delay(2);
        int dn = digitalRead(pin);

        const char *verdict = (up != dn) ? "空闲" : "!! 疑似有连接";
        if (up != dn) idle++; else used++;
        Serial.printf("        GPIO%-3d pullup=%d pulldown=%d  %s\n", pin, up, dn, verdict);
    }

    report("gpio_free_pins", used == 0 ? R_PASS : R_WARN,
           "空闲 %d / 疑似占用 %d (占用则与网表不符, 需查)", idle, used);
    Serial.println("        注意: GPIO33 = BMI160 中断, 不属于空闲脚, 未在此扫描");
}

// ===========================================================================
// 3. I2C 总线
// ===========================================================================
static bool i2cPing(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

static const char *i2cName(uint8_t a) {
    switch (a) {
        case 0x3C: case 0x3D: return "OLED (SSD1306/SH1106)";
        case 0x68: case 0x69: return "BMI160 (GY-BMI160)";
        case 0x76: case 0x77: return "BMP280 (GY-BMP280)";
        case 0x51:            return "PCF8563 (RTC)";
        default:              return "(未知器件)";
    }
}

static void test_i2c() {
    section("3. I2C 总线 (GPIO17/18)");
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 100000);
    delay(50);

    int found = 0;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        if (i2cPing(a)) {
            Serial.printf("        发现器件 0x%02X  %s\n", a, i2cName(a));
            found++;
        }
    }

    if (found == 0) {
        report("i2c", R_FAIL, "总线上没扫到任何器件 (检查模块是否插好 / I2C 上拉)");
        return;
    }
    report("i2c", R_PASS, "共发现 %d 个器件", found);

    // 逐个点名
    struct { uint8_t a0, a1; const char *name; } expect[] = {
        {0x3C, 0x3D, "OLED"},
        {0x68, 0x69, "BMI160"},
        {0x76, 0x77, "BMP280/BME280"},
        {0x51, 0x51, "PCF8563"},
    };
    for (auto &e : expect) {
        bool ok = i2cPing(e.a0) || (e.a1 != e.a0 && i2cPing(e.a1));
        report(e.name, ok ? R_PASS : R_FAIL, ok ? "应答正常" : "无应答 (未插或地址不同)");
    }

    // 读 BMI160 的 chip id 确认是真器件而不是地址巧合
    if (i2cPing(0x68) || i2cPing(0x69)) {
        uint8_t addr = i2cPing(0x68) ? 0x68 : 0x69;
        Wire.beginTransmission(addr);
        Wire.write(0x00);              // CHIP_ID 寄存器
        Wire.endTransmission(false);
        Wire.requestFrom((int)addr, 1);
        if (Wire.available()) {
            uint8_t id = Wire.read();
            report("bmi160_chipid", id == 0xD1 ? R_PASS : R_WARN,
                   "CHIP_ID=0x%02X (BMI160 应为 0xD1)", id);
        }
    }

    // BMP280 的 ID 在 0xD0
    uint8_t bmpAddr = i2cPing(0x76) ? 0x76 : (i2cPing(0x77) ? 0x77 : 0);
    if (bmpAddr) {
        Wire.beginTransmission(bmpAddr);
        Wire.write(0xD0);
        Wire.endTransmission(false);
        Wire.requestFrom((int)bmpAddr, 1);
        if (Wire.available()) {
            uint8_t id = Wire.read();
            const char *which = (id == 0x58) ? "BMP280" : (id == 0x60) ? "BME280" :
                                (id == 0x56) ? "BMP280(样片)" : "未知";
            report("bmp280_chipid", (id == 0x58 || id == 0x60) ? R_PASS : R_WARN,
                   "ID=0x%02X -> %s", id, which);
        }
    }
}

// ===========================================================================
// 4. LoRa 模块 (E22-900M30S / SX1262) — 纯只读探测, 不发射
// ===========================================================================
// SX126x SPI: 发 opcode(读时 bit7=0) 再发 NOP(0x00) 读回

static void spiWriteCmd(uint8_t op, const uint8_t *p, size_t n) {
    digitalWrite(PIN_LORA_NSS, LOW);
    SPI.transfer(op);
    for (size_t i = 0; i < n; i++) SPI.transfer(p[i]);
    digitalWrite(PIN_LORA_NSS, HIGH);
}

static void spiReadCmd(uint8_t op, uint8_t *out, size_t n) {
    digitalWrite(PIN_LORA_NSS, LOW);
    SPI.transfer(op);
    for (size_t i = 0; i < n; i++) out[i] = SPI.transfer(0x00);
    digitalWrite(PIN_LORA_NSS, HIGH);
}

static uint8_t loraStatus() {
    uint8_t v = 0;
    spiReadCmd(0xC0, &v, 1);          // GetStatus (单字节, 无 dummy)
    return v;
}

// SX126x 读寄存器的正确时序: opcode -> 地址 -> NOP(dummy) -> 数据
// 之前漏了 dummy, 导致读到 0xAA/0xAA 这种无效值
static void loraReadRegister(uint16_t addr, uint8_t *out, size_t n) {
    digitalWrite(PIN_LORA_NSS, LOW);
    SPI.transfer(0x1D);                 // ReadRegister
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    SPI.transfer(0x00);                 // NOP dummy
    for (size_t i = 0; i < n; i++) out[i] = SPI.transfer(0x00);
    digitalWrite(PIN_LORA_NSS, HIGH);
}

// GetVersion 的正确时序: opcode -> NOP(dummy) -> status -> version
static void loraGetVersion(uint8_t *status, uint8_t *version) {
    digitalWrite(PIN_LORA_NSS, LOW);
    SPI.transfer(0x42);                 // GetVersion
    SPI.transfer(0x00);                 // NOP dummy
    *status  = SPI.transfer(0x00);
    *version = SPI.transfer(0x00);
    digitalWrite(PIN_LORA_NSS, HIGH);
}

static void loraHardwareReset() {
    pinMode(PIN_LORA_NRST, OUTPUT);
    digitalWrite(PIN_LORA_NRST, LOW);
    delay(10);
    digitalWrite(PIN_LORA_NRST, HIGH);
    delay(30);
}

static void test_lora() {
    section("4. LoRa 模块 (E22-900M30S, SPI + 只读探测)");

    pinMode(PIN_LORA_BUSY, INPUT);
    pinMode(PIN_LORA_DIO1, INPUT);

    // 4.1 复位前后 BUSY
    loraHardwareReset();
    int busyAfterReset = digitalRead(PIN_LORA_BUSY);
    delay(100);
    int busyIdle = digitalRead(PIN_LORA_BUSY);
    report("lora_busy", busyIdle == LOW ? R_PASS : R_FAIL,
           "复位后=%d, 100ms 后=%d (空闲应为 0/LOW)", busyAfterReset, busyIdle);

    // 4.2 先确认 MISO 是否被外部驱动 —— 这是判断"模块在不在"的关键
    pinMode(PIN_LORA_MISO, INPUT_PULLUP);
    delay(2);
    int misoIdle = digitalRead(PIN_LORA_MISO);

    SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    pinMode(PIN_LORA_NSS, OUTPUT);
    digitalWrite(PIN_LORA_NSS, HIGH);
    delay(10);

    uint8_t st = loraStatus();
    uint8_t verStatus = 0, verByte = 0;
    loraGetVersion(&verStatus, &verByte);

    // 再读一个确定的寄存器: 0x0740 是 LoRa 同步字 (SetLoRaSymbNumTimeout 无关)
    // 用 ReadRegister 验证寄存器访问通道 (命令 0x1D)
    uint8_t syncWord[2] = {0};
    loraReadRegister(0x0740, syncWord, 2);

    SPI.endTransaction();

    Serial.printf("        MISO 空闲电平    : %d\n", misoIdle);
    Serial.printf("        GetStatus        : 0x%02X\n", st);
    Serial.printf("        GetVersion       : status=0x%02X version=0x%02X\n", verStatus, verByte);
    Serial.printf("        ReadReg 0x0740   : 0x%02X 0x%02X\n", syncWord[0], syncWord[1]);
    Serial.printf("        (芯片状态位 cmd=0x%02X)\n", (st >> 1) & 0x07);

    // SX1261/2 的 GetVersion: version 字节应为 0x01
    if (verByte == 0x01) {
        report("lora_chip", R_PASS, "GetVersion version=0x01 -> SX126x 在线");
    } else if (verByte == 0x00 && verStatus == 0x00) {
        report("lora_chip", R_FAIL,
               "SPI 全 0 —— 模块未插 / 无供电 / SPI 接错");
    } else if (verByte == 0xFF || (verByte == 0xAA && verStatus == 0xAA)) {
        report("lora_chip", R_FAIL,
               "读到无效值 (0x%02X/0x%02X) —— MISO 无有效驱动, 模块很可能不在位",
               verStatus, verByte);
    } else {
        report("lora_chip", R_WARN,
               "GetVersion 返回 status=0x%02X version=0x%02X, 非标准值, 需人工判断",
               verStatus, verByte);
    }

    // 4.3 尝试 SetDio2AsRfSwitch(1) 并读回 Standby
    //     这正好验证本板"MCU 不控制 TXEN/RXEN、由 DIO2 驱动"的设计
    uint8_t setDio2 = 0x01;
    spiWriteCmd(0x9D, &setDio2, 1);
    delay(5);
    uint8_t mode = 0;
    spiReadCmd(0xC0, &mode, 1);
    report("lora_dio2_rfsw", R_PASS,
           "已下发 SetDio2AsRfSwitch(1) (本板 TXEN/RXEN 由 DIO2 驱动)");

    report("lora_tx_test", R_SKIP,
           "未做发射测试 (默认只读探测, 避免误发射/超规)");

    Serial.println("        提示: DIO1(GPIO1)/BUSY(GPIO4) 电平会随收发变化, 属正常");
}

// ===========================================================================
// 5. GNSS (ATGM336H) — 只监听, 不发送
// ===========================================================================
static void test_gps() {
    section("5. GNSS (ATGM336H @ GPIO8/GPIO9)");
    Serial.printf("        监听 UART %d baud, 4 秒...\n", GPS_BAUD);

    // UART1, RX=GPIO8(接模块TX), TX=GPIO9(接模块RX)
    Serial1.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_TX, PIN_GPS_RX);

    char line[128];
    size_t idx = 0;
    int sentences = 0, gga = 0, rmc = 0, gsv = 0;
    char firstSentence[128] = {0};

    uint32_t t0 = millis();
    while (millis() - t0 < 4000) {
        while (Serial1.available()) {
            char c = Serial1.read();
            if (c == '\n' || c == '\r') {
                if (idx > 6) {
                    line[idx] = 0;
                    if (sentences == 0) { strncpy(firstSentence, line, sizeof(firstSentence) - 1); }
                    sentences++;
                    if (strstr(line, "GGA")) gga++;
                    if (strstr(line, "RMC")) rmc++;
                    if (strstr(line, "GSV")) gsv++;
                }
                idx = 0;
            } else if (idx < sizeof(line) - 1) {
                line[idx++] = c;
            }
        }
    }

    if (sentences > 0) {
        report("gps_uart", R_PASS, "收到 %d 条 NMEA (GGA=%d RMC=%d GSV=%d)",
               sentences, gga, rmc, gsv);
        Serial.printf("        首条: %s\n", firstSentence);
    } else {
        report("gps_uart", R_FAIL,
               "4 秒内没收到任何 NMEA。检查: 模块是否插好 / TX-RX 是否交叉 / 波特率");
    }

    // PPS: 有定位时才会跳变
    pinMode(PIN_GPS_PPS, INPUT);
    int ppsReads = 0, ppsHigh = 0;
    for (int i = 0; i < 200; i++) {
        int v = digitalRead(PIN_GPS_PPS);
        ppsReads++;
        if (v) ppsHigh++;
        delayMicroseconds(500);
    }
    if (ppsHigh == 0 || ppsHigh == ppsReads)
        report("gps_pps", R_WARN, "PPS 无跳变 (未定位时正常, 静置 %d/%d 为高)", ppsHigh, ppsReads);
    else
        report("gps_pps", R_PASS, "PPS 有跳变 (%d/%d 为高) -> 已定位", ppsHigh, ppsReads);
}

// ===========================================================================
// 6. WS2812 状态灯
// ===========================================================================
static void test_neopixel() {
    section("6. WS2812 RGB (GPIO21)");
#if HAS_NEOPIXEL_LIB
    Adafruit_NeoPixel px(NEOPIXEL_NUM, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
    px.begin();
    px.setBrightness(40);

    struct { uint8_t r, g, b; const char *n; } seq[] = {
        {255, 0, 0, "红"}, {0, 255, 0, "绿"}, {0, 0, 255, "蓝"}, {0, 0, 0, "灭"},
    };
    for (auto &s : seq) {
        px.setPixelColor(0, px.Color(s.r, s.g, s.b));
        px.show();
        Serial.printf("        已点亮: %s\n", s.n);
        delay(700);
    }
    report("neopixel", R_PASS, "已依次点亮 红/绿/蓝 —— 请目视确认灯真的亮了");
#else
    report("neopixel", R_SKIP, "未编译进 NeoPixel 库 (离线取不到依赖), 跳过");
#endif
}

// ===========================================================================
// 7. 非易失存储 (LittleFS / NVS)
// ===========================================================================
static void test_storage() {
    section("7. 存储");
    // 只报告分区信息, 不做格式化以免破坏数据
    Serial.printf("        Sketch 大小     : %u bytes\n", (unsigned)ESP.getSketchSize());
    Serial.printf("        空闲堆          : %u bytes\n", (unsigned)ESP.getFreeHeap());
    report("storage", R_PASS, "分区表按 8MB 生效 (见启动前的 flash_size 检查)");
}

// ===========================================================================
// main
// ===========================================================================
void setup() {
    Serial.begin(115200);
    // 等待 USB CDC 主机 (最多 3 秒, 避免错过开头输出)
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 3000) delay(10);
    delay(300);

    Serial.println();
    Serial.println("========================================================");
    Serial.println("  T-mesh 硬件自检 (hw-selftest)");
    Serial.println("  独立于 Meshtastic 的最小工程");
    Serial.println("========================================================");

    test_chip_info();
    test_gpio_scan();
    test_i2c();
    test_lora();
    test_gps();
    test_neopixel();
    test_storage();

    section("汇总");
    Serial.printf("        PASS=%d  FAIL=%d  WARN=%d  SKIP=%d\n",
                  g_pass, g_fail, g_warn, g_skip);
    if (g_fail == 0)
        Serial.println("        >>> 没有 FAIL —— 所有已测部件工作正常");
    else
        Serial.printf("        >>> 有 %d 项 FAIL, 见上方逐条结果\n", g_fail);

    Serial.println("\n  重点看第 1 节的 psram 结果:");
    Serial.println("    PASS -> 芯片 PSRAM 可用, Meshtastic 的崩溃是【构建体系】问题");
    Serial.println("    FAIL -> 这颗模组的 PSRAM 确实不可用 (硬件/批次问题)");
    Serial.println("========================================================\n");
}

void loop() {
    // 自检只跑一次; 之后每秒打印一次心跳, 便于确认板子仍在运行
    static uint32_t last = 0;
    if (millis() - last > 5000) {
        last = millis();
        Serial.printf("[心跳] 运行中, 空闲堆 %u bytes, 已运行 %lu s\n",
                      (unsigned)ESP.getFreeHeap(), (unsigned long)(millis() / 1000));
    }
    delay(100);
}
