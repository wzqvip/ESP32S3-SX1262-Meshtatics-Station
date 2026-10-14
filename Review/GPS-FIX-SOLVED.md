# GPS 排查记录 —— 已解决

> **状态：已解决。GPS 定位正常（11 颗卫星 / 3D fix），且射频收发已实测通。**

---

## 0. 一句话结论

**根因是我的 variant 把 `GPS_RX_PIN` / `GPS_TX_PIN` 定义反了。**

```c
// ❌ 错误（我原来写的）
#define GPS_TX_PIN 8    // 注释写对了"ESP32 接收"，但宏名反了
#define GPS_RX_PIN 9

// ✅ 修正
#define GPS_RX_PIN 8    // ESP32 接收 <- U6.3 (模块 TX)
#define GPS_TX_PIN 9    // ESP32 发送 -> U6.4 (模块 RX)
```

后果链：UART 从 GPIO9 读 → 一条 NMEA 都收不到 → 自动探测全部失败 →
`No GNSS Module` → `GPS::setup()` 返回 false → **GPS 被完全禁用**。

---

## 1. 关键：宏名指的是【ESP32 这一侧】的方向

这是最容易搞错的点。上游 `GPS.cpp`：

```c
int8_t _rx_gpio = config.position.rx_gpio;
int8_t _tx_gpio = config.position.tx_gpio;
#if defined(GPS_RX_PIN)
    if (!_rx_gpio) _rx_gpio = GPS_RX_PIN;    // ESP32 从【这个脚】接收
#endif
#if defined(GPS_TX_PIN)
    if (!_tx_gpio) _tx_gpio = GPS_TX_PIN;    // ESP32 从【这个脚】发送
#endif
```

日志里 `Use GPIO8 for GPS RX` 印证了这一点 —— 是 ESP32 的 RX，不是模块的。

**接线（飞针数据）：**

```
U6.3 = 模块的 TX ──> GPIO8   =>  GPIO8 是 ESP32 的 RX
U6.4 = 模块的 RX <── GPIO9   =>  GPIO9 是 ESP32 的 TX
```

---

## 2. 怎么定位到的：不靠推理，靠实测

写了独立工程 `gps-diag/`，把几种组合全试一遍，用数据说话：

| 组合 | 结果 |
| --- | --- |
| **rx=8 tx=9 @9600** | ✅ **收到 51 条 NMEA**（GGA=4 RMC=4 GSV=20） |
| rx=9 tx=8 @9600 | ❌ 无 |
| rx=8 tx=9 @38400 | ❌ 无 |
| rx=8 tx=9 @115200 | ❌ 无 |

首条语句 `$GNGSA,A,3,...` 里的 `3` = **3D 定位**，说明模块工作良好。

同时还测了**主动发探测指令**：

```
rx=8 tx=9  发 $PCAS06,1*1A  ->  应答 0 条
rx=9 tx=8  发 $PCAS06,1*1A  ->  应答 0 条
```

**这个模块根本不响应上游的探测指令** —— 它是"只输出 NMEA、不应答查询"的类型。

---

## 3. 附带修的第二个问题：上游探测对纯 NMEA 模块没有兜底

上游的自动探测依赖模块对 `$PDTINFO` / `$PCAS06,1*1A` / `$PMTK605*31` /
`$PAIR021*39` / UBX `MONVER` 的**应答**。本板的 ATGM336H 都不应答 →
`gnssModel` 留在 `UNKNOWN` → `GPS::setup()` 里：

```c
if (gnssModel != GNSS_MODEL_UNKNOWN) { setConnected(); ... }
else { return false; }        // 彻底不启用
```

而且 `GNSS_MODEL_GENERIC_NMEA` 只用于 gpsd（网络 NMEA），
**串口纯 NMEA 模块没有对应的兜底路径**。

### 我加的兜底（`src/gps/GPS.cpp`）

在 `probe()` 的 case 0 里、**在发 `$PCAS03`（关闭 NMEA 输出）之前**，
被动读 1.2 秒；若能解析出成句的 NMEA（≥3 条且含 GGA/RMC 这类定位语句），
就认定为纯 NMEA 源：

```c
if (sentences >= 3 && sawFixSentence) {
    LOG_INFO("Plain NMEA stream detected (%d sentences) -> treating as ATGM336H", sentences);
    return GNSS_MODEL_ATGM336H;
}
```

> **位置很关键**：case 0 紧接着会发 `$PCAS03,0,...` 把 AT6558 系的 NMEA 全关掉，
> 放到那之后再读就什么都看不到了。

### 我加的第二道保护：有硬件定义时不让 App 覆盖引脚

`GPS.cpp` 里 `config.position.rx_gpio`（存在 NVS、App 可改）会**覆盖** variant 的定义。
串口 GPS 的引脚是 PCB 走线固定的，App 里配错不会带来任何好处，只会让 GPS 静默失效。
所以加了：

```c
#if defined(GPS_RX_PIN)
    if (_rx_gpio != GPS_RX_PIN) {
        LOG_WARN("GPS RX GPIO override: config=%d -> variant GPS_RX_PIN=%d", ...);
        _rx_gpio = GPS_RX_PIN;
    }
#endif
```

这道保护在排查中直接**排除了"App 配置错"的可能** —— 日志显示 `config=0`，
证明 App 侧配置一直是空的，问题 100% 在 variant。

---

## 4. 修好后的实测结果

```
WARN  | GPS RX GPIO override: config=0 -> variant GPS_RX_PIN=8     ✅ 用对引脚了
DEBUG | Use GPIO8 for GPS RX / Use GPIO9 for GPS TX
INFO  | [GPS] Plain NMEA stream detected (17 sentences) -> treating as ATGM336H
DEBUG | [GPS] NMEA GPS time set 2026-10-14 20:56:51
DEBUG | [GPS] New GPS pos: lat=39.955662 lon=-83.165815 alt=290 pdop=1.41 sats=11
DEBUG | [GPS] Took 23s to get lock
INFO  | [GPS] GPS power state ACTIVE -> HARDSLEEP        ← 定位后自动休眠省电
```

**11 颗卫星、3D 定位、PDOP 1.41（很好）。**

---

## 5. 顺带确认：射频真的通了

GPS 修好后的日志里出现了真实 mesh 流量：

```
[RadioIf] Lora RX (id=0xf030aa46 fr=0xec070ef1 ... rxSNR=6.25 hopStart=7)
[RadioIf] Packet RX: 641ms
[Router] Received DeviceTelemetry from=0xec070ef1 ... battery_level=101, voltage=4.224
[Router] Received nodeinfo from=0x918af6dc  user Tacoin/taco
[Router] Rebroadcast msg from dc                        ← 中继转发
[RadioIf] Started Tx / Packet TX: 968ms / Completed sending   ← 真的发出去
[RadioIf] Completed sending
```

**收包、解密、路由、中继转发、发送全部工作。** 这块基站已经可用了。

---

## 6. 教训

1. **宏名要看清是哪一侧的视角。** `GPS_RX_PIN` 是 ESP32 的 RX，不是模块的。
   我在注释里写对了、宏名却写反了 —— 这种"注释对、代码错"最难发现。
2. **不确定的引脚接线要用实测判定，别推理。** `gps-diag/` 那个独立工程
   21 秒就编译完，一次烧录就把 4 种组合全试完，比读代码猜快得多。
3. **注意探测顺序中的副作用。** 兜底逻辑必须放在"关闭 NMEA 输出"指令之前，
   否则模块已经被静音，什么都读不到。

---

## 7. 相关文件

| 文件 | 说明 |
| --- | --- |
| `gps-diag/`（仓库根） | 引脚/波特率诊断工程，可独立编译烧录 |
| `Review/meshtastic-variant/upstream-modified/GPS.cpp` | 含两处改动的上游文件副本 |
| `Review/meshtastic-variant/tmesh-s3-e22/variant.h` | 修正后的引脚定义 |
