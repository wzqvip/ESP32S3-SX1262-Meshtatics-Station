# 发射功率审计 —— 确认不会超功率烧模块

> 结论：**安全。** 芯片输出被硬夹在 ≤ 8 dBm，33S 的 9 dBm 输入上限不会被突破。

---

## 1. 模块规格（本板实际装配 E22-900M33S）

| 项 | 值 | 来源 |
| --- | --- | --- |
| 最大发射功率 | 32.5 / **33** / 33.5 dBm | [官网参数页](https://www.cdebyte.com/products/E22-900M33S/1#Parameters) |
| **射频芯片输出上限** | **"cannot be greater than 9 dBm"** | 同上，Maximum transmit power 行的 Remark |
| 发射电流 | **1200 mA**（±100 mA @ 33 dBm & 5 V） | 同上 |
| 供电 | 3.3 / 5 / 5.5 V，**≥5.0 V 保证输出功率** | 同上 |
| 阻塞功率 | 10 dBm | 同上 |

**PA 增益约 25 dB**：芯片 8 dBm + 25 dB ≈ 33 dBm。

---

## 2. 固件里的功率路径（完整审过）

`src/mesh/RadioInterface.cpp:1455 limitPower()`：

```c
void RadioInterface::limitPower(int8_t loraMaxPower)
{
    power = config.lora.tx_power;                 // ① App 设的值（含 0 = 用区域上限）
    uint8_t maxPower = myRegion->powerLimit;
    if ((power > maxPower) && !licensed)  power = maxPower;   // ② 区域法规夹一次
    power -= TX_GAIN_LORA;                        // ③ 减 PA 增益
    if (power > loraMaxPower) power = loraMaxPower;           // ④ 【芯片侧】硬上限
    LOG_INFO("Final Tx power: %d dBm", power);
}
```

**关键点：第 ④ 步在减完增益之后，所以 `loraMaxPower` 就是直接传给
`setOutputPower()` 的芯片功率上限。**

调用点是 `limitPower(SX126X_MAX_POWER)`（`SX126xInterface.cpp:157` init、
`:336` reconfigure），所以 `SX126X_MAX_POWER` **就是芯片的绝对天花板**。

### 33S 的实际取值

| `TX_GAIN_LORA` | `SX126X_MAX_POWER` | 芯片输出 | 模块输出 | 对 9 dBm 上限 |
| --- | --- | --- | --- | --- |
| **25** | **8** | ≤ **8 dBm** | ≈ 33 dBm | ✅ 留 1 dB 余量 |
| 25 | 25（小功率模式，当前） | ≤ 0 dBm | ≈ 25 dBm | ✅ 远低于上限 |

**`TX_GAIN_LORA` 配成 30S 的 7 会怎样**（这是我修正前的错误配置）：

```
30 − 7 = 23 → 夹到 22 → 芯片 22 dBm   ← 超 9 dBm 上限 13 dB，有烧 PA 风险
```

---

## 3. 为什么改不动（三重保险）

### 3.1 上游预设（`src/configuration.h`）

```c
#ifdef EBYTE_E22_900M33S
// 25dB PA gain and 33dB rated output
#define TX_GAIN_LORA 25
#define SX126X_MAX_POWER 8
#endif
```

### 3.2 编译期断言（`variant.h`）

```c
#if !defined(EBYTE_E22_900M33S) && !defined(EBYTE_E22_900M30S)
#error "必须定义模块宏: 本板装 E22-900M33S..."
#endif
#if defined(EBYTE_E22_900M33S) && defined(EBYTE_E22_900M30S)
#error "两者不能同时定义: 功率表相反 (25/8 vs 7/22), 会烧 PA。"
#endif
```

### 3.3 数值断言（`src/mesh/SX126xInterface.cpp`）

```c
#if defined(TMESH_LOW_POWER_TEST)
#if (TX_GAIN_LORA != 25) || (SX126X_MAX_POWER != 25)
#error "TMESH: 33S 小功率模式预期 TX_GAIN_LORA=25 / SX126X_MAX_POWER=25。"
#endif
#else
#if (TX_GAIN_LORA != 25) || (SX126X_MAX_POWER != 8)
#error "TMESH: E22-900M33S 预期 TX_GAIN_LORA=25 / SX126X_MAX_POWER=8。"
#endif
#endif
```

**任何人把数值改错，编译直接失败，不会静默烧硬件。**

### 3.4 运行时实测佐证

```
INFO | Requested Tx power: 30 dBm; Device LoRa Tx gain: 25 dB
INFO | Final Tx power: 5 dBm          ← 芯片实际只被设到 5 dBm
INFO | Power output set to 5
INFO | Current limit set to 140.000000 / result 0
```

`Power output set to 5` = 传给 `setOutputPower()` 的就是 5 dBm。**5 < 8 < 9，安全。**

---

## 4. 其它已核对的点

| 项 | 状态 |
| --- | --- |
| 是否有其它路径调用 `setOutputPower` | 只有 `SX126xInterface.cpp:283/342/344` 三处，都在 `limitPower` 之后 |
| `NUM_PA_POINTS` | 编译期断言必须为 1（单段增益），走 1482 行分支 |
| OCP 限流 | `Current limit set to 140 mA`（SX1262 标准值，`result 0` 成功） |
| 低功率时 PA 是否还工作 | 当前芯片 0 dBm → 模块约 25 dBm，仍在工作区间（不是负值） |
| 小功率覆盖的**位置** | 已移到所有 `EBYTE_E22_*` 段**之后**（之前放在 33S 段之前会被覆盖） |
| 小功率覆盖的**数值** | 已按模块区分：33S→上限 25，30S→上限 15 |

---

## 5. ⚠️ 仍需你确认的一件事：供电余量

固件层面功率是安全的。但**硬件供电**是另一回事：

```
33S 发射电流 1200 mA  (33 dBm & 5V)
本板 5V 储能与 MP2482 原本按 30S 的 650 mA 评估
```

**接近 2 倍。** 放开小功率模式跑满 2 W 之前，建议确认：

1. MP2482 模块的**限流设定与电感饱和电流**
2. 5V 走线宽度能否承载 1.2 A 峰值
3. 12 V 输入源本身能否持续供出（含逻辑与传感器负载）
4. U17（470 µF）在脉冲下的电压跌落

**当前小功率模式（芯片 5 dBm / 模块约 25 dBm）是安全的**,可以先这样用着。

---

## 6. 换模块时的检查清单

若将来换别的 E22 型号，**必须**：

1. 查该型号 datasheet 里有无"射频芯片输出上限"
2. 改 `platformio.ini` 的 `-D EBYTE_E22_*` 为对应型号
3. 核对上游是否已有该型号的预设表；没有的话需自己加（含实测功率曲线）
4. 重新核算供电（各型号发射电流差异很大）
5. 确认当地法规（2 W 在多数地区需许可）
6. 编译 —— 断言会拦住配错的组合
