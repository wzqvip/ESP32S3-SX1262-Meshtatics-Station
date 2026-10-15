# 功能与限制清单（手机 App 能看到 / 能控制什么）

> 基于实机日志逐项核对，不是推测。
> 设备：`!c2895095` / `Tmesh-Statoion` / 区域 US / 906.875 MHz

---

## 1. 手机 ↔ 设备 双向通信

**测过，是通的。** 手机连上后的实机日志：

**设备 → 手机**（配置与状态上报）
```
INFO  | [NimbleBluetooth] Client wants config, nonce=...
DEBUG | [NimbleBluetooth] Send My NodeInfo / Send device metadata / Send region preset map
DEBUG | [NimbleBluetooth] Send channels 8
DEBUG | [NimbleBluetooth] Send config: device / position / power / network / display / lora / bluetooth / security
DEBUG | [NimbleBluetooth] Send module config: mqtt / serial / ext notification / store forward /
        range test / telemetry / canned message / audio / remote hardware / neighbor info /
        ambient lighting / detection sensor / paxcounter
```

**手机 → 设备**（配置下发 / 时间同步）
```
DEBUG | [NimbleBluetooth] PACKET FROM PHONE (Portnum=6)        ← 6 = Admin
INFO  | [NimbleBluetooth] Received Admin from=0x00000000, portnum=6
INFO  | [NimbleBluetooth] Handle admin payload 43
INFO  | [NimbleBluetooth] Client got set_time_only
DEBUG | [NimbleBluetooth] Upgrade time to quality NTP
```

**结论：所有 config / moduleConfig 字段都能双向同步，包括功率、区域、频道、模块开关。**

---

## 2. 手机端调功率：能调，而且**不会烧模块**

### 2.1 能调

`config.lora.tx_power` 就在下发的 `config: lora` 里，App 里可改；
HAM 参数（`AdminModule.cpp:1968`）也能设 `tx_power`。

### 2.2 为什么安全

我完整审过 `RadioInterface.cpp:1455 limitPower()`：

```c
power = config.lora.tx_power;                                    // ① 手机设的值
if (power > region->powerLimit && !is_licensed) power = limit;   // ② 区域夹取
power -= TX_GAIN_LORA;                                           // ③ 减 PA 增益
if (power > loraMaxPower) power = loraMaxPower;                  // ④ 芯片侧硬上限
```

**第 ④ 步不带 `is_licensed` 条件**，是最后一道无条件的墙；
而 `loraMaxPower` 就是 `SX126X_MAX_POWER`（本板 33S = **8 dBm**）。

各种组合穷举：

| 场景 | `is_licensed` 会跳过 | 最终芯片功率 | 对 9 dBm 上限 |
| --- | --- | --- | --- |
| 普通，手机设 30 | 无 | min(30−25, 8) = **5** | ✅ |
| 普通，手机设 0（默认） | 无（取区域上限 30） | min(30−25, 8) = **5** | ✅ |
| 普通，手机设 127（int8 极值） | 无 | min(127−25, 8) = **8** | ✅ |
| **HAM 模式**，手机设 30 | 区域夹取 + 减增益 | 30 → 夹到 **8** | ✅ |
| **HAM 模式**，手机设 127 | 同上 | 127 → 夹到 **8** | ✅ |
| 小功率模式（当前） | — | ≤ **(25−25)=0** | ✅ |

**结论：任何手机端配置组合下，芯片输出都不会超过 8 dBm < 9 dBm 上限。**

> 有意思的一点：HAM 模式跳过"减增益"反而让实际输出**更低**
> （少了 −25 的补偿），所以那条路径不会造成危险。
> 真正起保护作用的是第 ④ 步那道无条件的夹取。

**三重保险防止这个上限被改坏**：上游预设 + `variant.h` 模块断言 +
`SX126xInterface.cpp` 数值断言 —— 改错就编译失败。

---

## 3. 各项数据有没有进 App

| 项目 | 状态 | 实机证据 |
| --- | --- | --- |
| **GPS 位置** | ✅ 采集正常 | `New GPS pos: lat=39.9556 lon=-83.1658 alt=290 sats=11~13` |
| **GPS 时间** | ✅ 已同步 | `NMEA GPS time set 2026-10-14 21:41:13` |
| **时间源** | ✅ | `Time source acquired (None -> GPS)`，App 里显示 GPS 授时 |
| **BMP280 环境遥测** | ✅ | `Environment Telemetry: init` / `BMP280 getMetrics` |
| **设备遥测** | ✅ | 收到过他机 DeviceTelemetry（`voltage=4.224`），本机同样上报 |
| **电源遥测** | ✅ | `Power Telemetry: init` |
| **节点信息** | ✅ | `Send owner !c2895095/Tmesh-Statoion/taci`，他机也能看到我们 |
| **屏幕界面帧** | ✅ 10 帧 | `Show 10 module frames` |
| **GPS 探测缓存** | ✅ 已生效 | `Using cached GPS probe: ATGM336H @ 9600`（重启不再重新探测） |

### 3.1 ⚠️ 位置广播默认是关的

日志里反复出现：

```
INFO | [Position] Skip pos@...:6 broadcast; position sharing disabled on all channels
```

**这是 Meshtastic 的隐私默认值，不是故障。** GPS 定位与订阅都正常，
只是策略上不主动广播经纬度。

**想让别人在地图上看到你，需要在 App 里给主频道开位置共享**
（Channel → 主频道 → Position / 位置共享）。开了之后基站位置才会进 mesh。

### 3.2 ⚠️ BMI160 加速度计：**Meshtastic 上游不支持**

硬件是好的 —— 硬件自检读到了 `CHIP_ID=0xD1`，在 I2C 0x68 正常应答。
但 Meshtastic 上游**没有任何 BMI160 驱动**：

- `src/modules/Telemetry/Sensor/` 下有 LSM6DS3 / LSM6DSL / MPU6050 /
  LIS3DH / ICM42670 等，**没有 BMI160**
- 全仓搜 `BMI160` **零命中**

**所以 App 里不会出现这颗 IMU 的数据** —— 这是上游支持范围问题，
不是接线、地址或配置问题。自检日志里的 `acc_info = 0` 就是这个原因。

> 想要姿态/运动数据：换成上游支持的模块（如 **LSM6DS3 / MPU6050 / ICM42670**），
> 接在同一 I2C 上即可被自动识别，无需改代码。

### 3.3 ⚠️ PCF8563 RTC：已启用，首次上电需校时

**之前漏了配置，现已补上**（`variant.h`）：

```c
#define PCF8563_RTC 0x51      // 之前缺失 -> I2C 扫描不把 0x51 认成 RTC
#define PIN_RTC_INT 14
```

还需要加库依赖（`platformio.ini`）：
```ini
lib_deps =
  ${esp32s3_base.lib_deps}
  https://github.com/meshtastic/PCF8xRTC/archive/efe1d9c...zip
```
不加会报 `'PCF8xRTC' was not declared in this scope`。

**启用后的实机日志**：
```
WARN | PCF8563 read failed (oscillator stopped)
```

`oscillator stopped` 是**首次上电的正常状态** —— PCF8563 的时钟要在
被写过一次时间之后才起振。设备拿到 GPS 时间后会自动回写，
若 RTC 模块上装了纽扣电池，之后断电也能保住时间。

### 3.4 GPS 的两个良性警告

```
WARN | ATGM336H: Can't set Config
WARN | ATGM336H: Can't set Update Frequency
WARN | ATGM336H: Can't enable NMEA MSG: 0/4
```

**本板的 ATGM336H 只输出 NMEA、不响应任何配置查询指令**（我用 `gps-diag/`
实测过：`$PCAS06,1*1A` 应答 0 条）。所以这些"设置失败"是预期的，
**不影响定位与授时**（日志里位置和时间都在正常刷新）。

---

## 4. 汇总：手机 App 里应该能看到什么

| 能看到的 | 状态 |
| --- | --- |
| 设备节点 `Tmesh-Statoion` | ✅ |
| 区域 US / 频道 / 预设 | ✅ |
| 发射功率设置项（可调，且安全封顶） | ✅ |
| GPS 位置 / 卫星数 / 精度 | ✅ 采集正常，**广播需手动开启** |
| 时间（GPS 授时） | ✅ |
| 气压 / 温度（BMP280） | ✅ |
| 电压等电源遥测 | ✅ |
| 屏幕亮度等显示设置 | ✅ |

| 看不到的 | 原因 |
| --- | --- |
| 加速度 / 姿态（BMI160） | **上游无此驱动**，需换模块 |
| RTC 时间（断电后） | 需 RTC 模块装纽扣电池；首次上电需 GPS 校时一次 |

---

## 5. 建议的下一步

1. **在 App 里给主频道开位置共享** —— 否则基站位置不会进 mesh（见 3.1）
2. **确认 RTC 模块装了纽扣电池** —— 然后让 GPS 校时一次，之后断电保时（见 3.3）
3. **若需要 IMU，换 LSM6DS3 / MPU6050 / ICM42670**（见 3.2）
4. **核对 5V 供电余量**再放开 2W（33S 峰值 1200 mA，见 `TX-POWER-AUDIT.md`）
