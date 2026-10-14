// ============================================================================
// GPS 引脚方向 / 波特率 诊断
// ============================================================================
// 背景:
//   硬件自检 (RX=GPIO8, TX=GPIO9, 9600) 收到了 51 条 NMEA -> 硬件与引脚可用。
//   但 Meshtastic 启动日志报 "Use GPIO9 for GPS RX" 然后 "No GNSS Module"
//   -> 说明 Meshtastic 实际用的是 RX=GPIO9 (被 config.position.rx_gpio 覆盖)。
//
// 本固件把几种组合都实测一遍, 用数据确定:
//   A) 纯监听, 不发任何东西 (排除"需要先发指令"的干扰)
//   B) 引脚对调 (RX=8/TX=9 vs RX=9/TX=8)
//   C) 波特率 9600 / 38400 / 115200
//   D) 主动发 ATGM336H 的探测指令 $PCAS06,1*1A, 看是否有应答
// ============================================================================

#include <Arduino.h>

static const int PIN_A = 8;   // 网表: U6.3 (模块 TX)
static const int PIN_B = 9;   // 网表: U6.4 (模块 RX)

struct Probe {
    const char *label;
    int  rxPin;
    int  txPin;
    long baud;
};

// 纯监听组合: 只设 RX, 不发数据 (把自己这边的 TX 也接上但不写)
static const Probe probes[] = {
    {"A1 rx=8  tx=9   9600",    PIN_A, PIN_B, 9600},
    {"A2 rx=9  tx=8   9600",    PIN_B, PIN_A, 9600},
    {"A3 rx=8  tx=9  38400",    PIN_A, PIN_B, 38400},
    {"A4 rx=8  tx=9 115200",    PIN_A, PIN_B, 115200},
};

static int listenFor(HardwareSerial &ser, int rx, int tx, long baud, uint32_t ms,
                     char *firstOut, size_t firstLen, int *gga, int *rmc, int *gsv)
{
    ser.begin(baud, SERIAL_8N1, rx, tx);
    delay(50);
    while (ser.available()) ser.read();   // 清空

    char line[128];
    size_t idx = 0;
    int sentences = 0;
    bool gotFirst = false;
    if (gga) *gga = 0;
    if (rmc) *rmc = 0;
    if (gsv) *gsv = 0;

    uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        while (ser.available()) {
            char c = ser.read();
            if (c == '\n' || c == '\r') {
                if (idx > 6) {
                    line[idx] = 0;
                    if (!gotFirst) {
                        gotFirst = true;
                        if (firstOut && firstLen) {
                            strncpy(firstOut, line, firstLen - 1);
                            firstOut[firstLen - 1] = 0;
                        }
                    }
                    sentences++;
                    if (gga && strstr(line, "GGA")) (*gga)++;
                    if (rmc && strstr(line, "RMC")) (*rmc)++;
                    if (gsv && strstr(line, "GSV")) (*gsv)++;
                }
                idx = 0;
            } else if (idx < sizeof(line) - 1) {
                line[idx++] = c;
            }
        }
    }
    ser.end();
    return sentences;
}

void setup() {
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 3000) delay(10);
    delay(200);

    Serial.println();
    Serial.println("======================================================");
    Serial.println("  GPS 诊断 (引脚方向 / 波特率)");
    Serial.println("======================================================");
    Serial.printf("  网表:  U6.3(模块TX) -> GPIO%d    U6.4(模块RX) -> GPIO%d\n", PIN_A, PIN_B);
    Serial.printf("  自检时用的: RX=GPIO%d  TX=GPIO%d @9600 -> 收到过 NMEA\n", PIN_A, PIN_B);
    Serial.printf("  Meshtastic 日志说: Use GPIO%d for GPS RX\n", PIN_B);
    Serial.println();

    // ---- A 组: 纯监听 ----
    Serial.println("--- A 组: 纯被动监听 (不发任何数据) ---");
    for (auto &p : probes) {
        char first[128] = {0};
        int gga = 0, rmc = 0, gsv = 0;
        HardwareSerial ser(1);
        int n = listenFor(ser, p.rxPin, p.txPin, p.baud, 4000, first, sizeof(first), &gga, &rmc, &gsv);
        if (n > 0) {
            Serial.printf("  [收到!] %-22s 语句=%d (GGA=%d RMC=%d GSV=%d)\n", p.label, n, gga, rmc, gsv);
            Serial.printf("          首条: %s\n", first);
        } else {
            Serial.printf("  [无]    %-22s\n", p.label);
        }
    }

    // ---- B 组: 主动发探测指令, 看应答 ----
    Serial.println();
    Serial.println("--- B 组: 主动发 ATGM336H 探测指令 $PCAS06,1*1A ---");
    struct { int rx, tx; const char *tag; } act[] = {
        {PIN_A, PIN_B, "rx=8 tx=9"},
        {PIN_B, PIN_A, "rx=9 tx=8"},
    };
    for (auto &a : act) {
        HardwareSerial ser(1);
        ser.begin(9600, SERIAL_8N1, a.rx, a.tx);
        delay(50);
        while (ser.available()) ser.read();
        ser.write("$PCAS06,1*1A\r\n");
        ser.flush();
        delay(600);

        char line[128];
        size_t idx = 0;
        int n = 0;
        char first[128] = {0};
        bool got = false;
        uint32_t t1 = millis();
        while (millis() - t1 < 1500) {
            while (ser.available()) {
                char c = ser.read();
                if (c == '\n' || c == '\r') {
                    if (idx > 6) {
                        line[idx] = 0;
                        if (!got) { strncpy(first, line, sizeof(first) - 1); got = true; }
                        n++;
                    }
                    idx = 0;
                } else if (idx < sizeof(line) - 1) line[idx++] = c;
            }
        }
        ser.end();
        Serial.printf("  %-12s 应答语句=%d", a.tag, n);
        if (n > 0) Serial.printf("  首条: %s", first);
        Serial.println();
    }

    // ---- C 组: 引脚电平 (判断是否接反/悬空) ----
    Serial.println();
    Serial.println("--- C 组: 两个脚的空闲电平 (接上拉看是否被外部驱动) ---");
    for (int pin : {PIN_A, PIN_B}) {
        pinMode(pin, INPUT_PULLUP);  delay(5); int up = digitalRead(pin);
        pinMode(pin, INPUT_PULLDOWN); delay(5); int dn = digitalRead(pin);
        Serial.printf("  GPIO%-2d pullup=%d pulldown=%d  %s\n", pin, up, dn,
                      (up != dn) ? "(未接/悬空)" : "(有外部驱动 -> 接到了模块)");
    }

    Serial.println();
    Serial.println("======================================================");
    Serial.println("  结论看上面: 哪个组合收到 NMEA, 那个就是正确接法");
    Serial.println("======================================================");
    Serial.println("(之后每秒打印一次心跳)");
}

void loop() {
    static uint32_t last = 0;
    if (millis() - last > 5000) {
        last = millis();
        Serial.printf("[心跳] %lu s\n", (unsigned long)(millis() / 1000));
    }
    delay(100);
}
