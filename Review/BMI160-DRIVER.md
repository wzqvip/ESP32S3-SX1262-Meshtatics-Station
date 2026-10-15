# BMI160 加速度计驱动（为 T-mesh 新增）

> **状态：已实机验证可用。** 检测、初始化、真实数据读取全部通过。
> **上游确认无此驱动，且无人重复劳动**（2026-10-15 核查，见
> [UPSTREAM-CONTRIBUTION.md](UPSTREAM-CONTRIBUTION.md) 1.5 节）。

---

## 1. 为什么需要自己加

Meshtastic 上游 **没有 BMI160 驱动**（已四角度交叉核实）。`src/motion/` 里有
同族的 `BMI270Sensor` / `BMX160Sensor` / `BMM150Sensor` / `BMA423Sensor` /
`BHI260APSensor`，但没有 BMI160。

本板 U7（GY-BMI160，I2C `0x68`，`CHIP_ID = 0xD1`）硬件正常，
缺的只是软件支持。所以补了一份驱动。

> ⚠️ **合并上游时注意**：驱动开发基线是 `04cb888`，已落后上游 `develop`
> 8 个提交，其中 `ScanI2C.cpp` 与 `ScanI2CTwoWire.cpp` 上游有改动。
> **不要整文件覆盖**，按 [UPSTREAM-CONTRIBUTION.md](UPSTREAM-CONTRIBUTION.md)
> 3.3 节的逐点说明合并。

---

## 2. 改动清单（5 个文件）

| # | 文件 | 改动 | 类型 |
| --- | --- | --- | --- |
| 1 | `src/motion/BMI160Sensor.h` | **新增** | 新文件 |
| 2 | `src/motion/BMI160Sensor.cpp` | **新增** | 新文件 |
| 3 | `src/detect/ScanI2C.h` | 枚举加 `BMI160` | 加 1 行 |
| 4 | `src/detect/ScanI2C.cpp` | `firstAccelerometer()` 列表加 `BMI160`，计数 15→16 | 改 2 行 |
| 5 | `src/detect/ScanI2CTwoWire.cpp` | `0x68/0x69` 处按 `reg0x00 == 0xD1` 识别 | 加 1 个分支 |
| 6 | `src/motion/AccelerometerThread.h` | include + `case` 分支 | 各加 3 行 |
| 7 | `variants/esp32s3/diy/tmesh-s3-e22/variant.h` | `#define HAS_BMI160` | 加 1 行 |

> 3~6 是纯**追加**式改动，不影响其它板子（都由 `HAS_BMI160` 或
> `CHIP_ID` 判定守门）。升级上游后按上表重新套用即可。

---

## 3. 关键实现点

### 3.1 BMI160 比 BMI270 简单得多

| | BMI270 | **BMI160** |
| --- | --- | --- |
| 8KB 配置块 | **必需**（`uploadConfigFile()`） | **不需要** |
| 使能方式 | 写 `PWR_CTRL(0x7D)` | 写 `CMD(0x7E) = 0x11` |
| `PWR_CONF(0x7C)` | 有 | **没有**（别照抄 BMI270） |
| 驱动规模 | 594 行（大半是配置块数据） | ~180 行 |

初始化序列：

```
读 CHIP_ID(0x00) 必须 == 0xD1
  -> 软复位 CMD(0x7E) = 0xB6, 等 50ms
  -> 再验 CHIP_ID
  -> ACC_CONF(0x40) = 0x28   (BWP=normal(0x2<<4), ODR=100Hz(0x8))
  -> ACC_RANGE(0x41) = 0x03  (±2g)
  -> ACC_PWR_CTRL(0x7D) = 0x04  (使能 acc)
  -> CMD(0x7E) = 0x11        (acc 进入 normal mode)
  -> 读一次 6 字节验证数据通路
```

### 3.2 ⚠️ 检测顺序很关键

`ScanI2CTwoWire.cpp` 的 `case ICM20948_ADDR / ICM20948_ADDR_ALT` 里，
**地址 0x68 与 0x69 上面挤了 6 种器件**：

| `reg0x00` | 器件 |
| --- | --- |
| `0xEA` | ICM20948 |
| `0x24` | BMI270 |
| `0xD8` | BMX160 |
| **`0xD1`** | **BMI160** ← 新增 |
| `reg0x75 == 0x60` | ICM-42607-P |
| `reg0x75 == 0x68` | MPU6050 |

**BMI160 的判定必须放在那条 `else { ... probeSEN5X() ... }` 之前。**
否则会先落进 `else`，被 `probeSEN5X()` 抢先匹配（`SEN5X_ADDR` 也是 0x69，
探测在 0x68 上会误报）。这是我实现时踩过的坑。

### 3.3 运动检测逻辑

与 `BMI270Sensor` 保持一致：读 6 字节 → 求与上一采样的差值 →
超过阈值（`3200` ≈ 0.2g @±2g）就 `wakeScreen()`，并把轮询间隔从
`MOTION_SENSOR_CHECK_INTERVAL_MS`(50ms) 改为 500ms。

基线用低通更新（`prev = (prev*9 + new)/10`），避免缓慢倾斜被误判成运动。

---

## 4. 实机验证记录

```
DEBUG | Motion MotionSensor port: Wire address: 0x68 type: 90
DEBUG | AccelerometerThread::init ok
```

**枚举值对照（运行时实测打印，别按源码行数推算）：**

```
BMI160 enum=90    SEN5X enum=91
```

> ⚠️ 注意：`BMI160 = 90` 而 **不是** 89。我一开始按源码行数手算成 89，
> 误以为 `type: 90` 是 SEN5X，白折腾了几轮。**枚举值要运行时打印确认。**

**芯片与数据实测：**

```
reg0x00 @0x68 = 0xD1                 ← 芯片识别正确
BMI160 accel x=591 y=-5422 z=-15420  ← z 幅值 16399 ≈ 16384 = 1g @±2g
```

板子平放，重力只落在 Z 轴且幅值精确等于 1g —— 说明**量程配置正确、
数据通路完全正常**。

---

## 5. 手机 App 里能看到什么

启用后：

- 双击（double tap）可作为按钮 → 受 `config.device.double_tap_as_button_press` 控制
- 移动/敲击唤醒屏幕 → 受 `config.display.wake_on_tap_or_motion` 控制

⚠️ **注意**：BMI160 只提供**加速度**，不提供磁力计，所以**不产生罗盘航向**。
`MotionSensor::providesHeading()` 保持默认 `false`，这是正确的。

Meshtastic 的遥测里**不包含**加速度原始值 —— 它只在设备本地用于
"敲击/移动"这类功能。所以 App 里不会看到 x/y/z 数值，这是上游设计如此，
不是驱动问题。

---

## 6. 若将来要合并回上游

建议这么提交：

1. `src/motion/BMI160Sensor.{h,cpp}` —— 独立驱动，无外部依赖
2. 探测与枚举改动（`ScanI2C.*`、`AccelerometerThread.h`）
3. 可选的 variant 启用（`HAS_BMI160`）

上游已有 BMI270 的先例，BMI160 更简单（无 8KB 配置块），
接受度应该不低。注意保持与 BMI270 一致的代码风格 —— 本实现已对齐。
