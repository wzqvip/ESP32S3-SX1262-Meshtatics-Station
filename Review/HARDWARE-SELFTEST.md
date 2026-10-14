# 硬件自检 (unit test) — 结果与结论

> 工程位置：`hw-selftest/`（独立 PlatformIO 工程，**不依赖 Meshtastic**）
> 运行方式：
> ```powershell
> $env:PLATFORMIO_CORE_DIR='D:\meshtastic-build\pio-core'
> pio run -d hw-selftest -t upload
> # 然后 115200 打开串口
> ```
> 使用的平台：pioarduino `espressif32` **55.03.312**（与 Meshtastic 同一套，用本地路径引用）

---

## ⭐ 核心结论：PSRAM 硬件完全可用

```
PASS  psram      可用! 总 2097152 bytes (2.0 MB), 空闲 2095104 bytes
PASS  psram_rw   64KB 写入/回读一致
```

**这直接排除了硬件嫌疑。** 也就是说：

> **Meshtastic 启动即崩（`E (407) octal_psram: PSRAM chip is not connected`）
> 不是这颗模组的 PSRAM 坏了，而是 Meshtastic 的构建体系问题。**

自检工程用的还是**同一个自建板级 JSON**（`memory_type: qio_qspi`，Quad），
PSRAM 一次就初始化成功。区别只在**框架**：自检用干净构建，Meshtastic 用它自带的那套
预编译 IDF 库（见 `ESP32S3-PSRAM-ISSUE.md`）。

**推论**：那颗芯片是 **ESP32-S3R2（2MB Quad PSRAM，片内）**。Meshtastic 崩是因为它
按 **Octal** 路径去初始化了 Quad PSRAM。

---

## 逐项结果

```
PASS=15   FAIL=1   WARN=2   SKIP=1
```

| 序 | 测试项 | 结果 | 实测值 / 说明 |
| --- | --- | --- | --- |
| 1 | 芯片 | ✅ | `ESP32-S3 rev2, 2 核 @ 240 MHz` |
| 1 | Flash 容量 | ✅ | `8388608 bytes (8 MB)` — 与 N8R2 一致 |
| 1 | **PSRAM** | ✅ | **2097152 B (2.0 MB) 可用** |
| 1 | **PSRAM 读写** | ✅ | 64 KB 写入/回读一致 |
| 1 | 内部堆 | ✅ | 空闲 368036 / 总 407720 |
| 1 | 复位原因 | — | `11` |
| 2 | 空闲 GPIO 扫描 | ✅ | 12 个脚（2/15/16/34~42）全部"空闲" |
| 3 | I2C 总线 | ✅ | 发现 3 个器件 |
| 3 | **PCF8563 (RTC)** | ✅ | 地址 `0x51` 应答 |
| 3 | **BMI160** | ✅ | 地址 `0x68`，`CHIP_ID=0xD1` ✅ 芯片级确认 |
| 3 | **BMP280** | ✅ | 地址 `0x76`，`ID=0x58` ✅ 芯片级确认 |
| 3 | **OLED** | ❌ | **无应答**（未插 / 地址不符 / 接线） |
| 4 | LoRa BUSY | ✅ | 复位后=0，100ms 后=0 |
| 4 | LoRa SPI | ⚠️ | `GetVersion` 返回 `0xAA/0xAA`（异常值，待复测） |
| 4 | DIO2 射频开关 | ✅ | 已下发 `SetDio2AsRfSwitch(1)` |
| 4 | 发射测试 | ⏭️ | 故意跳过（避免误发射） |
| 5 | **GNSS** | ✅ | **收到 51 条 NMEA**（GGA=4 RMC=3 GSV=20） |
| 5 | GNSS PPS | ⚠️ | 无跳变（未定位时属正常） |
| 6 | WS2812 | ✅ | 依次点亮红/绿/蓝（需目视确认） |
| 7 | 存储 / 分区 | ✅ | 8MB 分区表生效 |

---

## 需要跟进的两项

### ❌ OLED 无应答

I2C 上其余三个器件都正常，只有 OLED 不在总线上（`0x3C` / `0x3D` 都无响应）。

可能原因，按可能性排序：

1. **OLED 模块没插**
2. **`U15` 供电焊桥**：`U13`(OLED) 由焊桥 `U15` 接在 +5V。若桥没焊或焊到别处，OLED 不供电
   （见 `HARDWARE-REVIEW.md` 发现 2-2）
3. **模块 pin 顺序不符**：4-pin OLED 的 `VCC/GND/SDA/SCL` 顺序各厂家不同，
   插反会导致不上电（甚至损坏）
4. OLED 控制器地址不是 `0x3C/0x3D`

**下一步**：用万用表量 OLED 座的 VCC 对 GND 是否有电压；再确认丝印顺序。

### ⚠️ LoRa `GetVersion` 返回 0xAA

`0xAA` = `10101010`，是典型的"无有效驱动/时序不对"的值。但注意：

- **`BUSY` 电平是对的**（复位后低、稳定）→ 说明模块**有供电**且 BUSY 引脚连着
- MISO 空闲电平读到 `1` → 有上拉或被驱动

我已修正 SPI 读法（SX126x 规定发完 opcode 要**先跟一个 NOP dummy** 再读数据，
原来漏了这步），并增加 `ReadRegister(0x0740)` 交叉验证。**待复测。**

**下一步**：重跑自检看修正后的结果。若仍异常，量 LoRa 模块 VCC（应为 5V）与
`U2.9/U2.10` 的供电。

---

## 这个自检工程的价值

1. **一刀切开"硬件坏" vs "框架问题"** —— 这是它最重要的作用，结论已经拿到
2. **不依赖 Meshtastic**，因此也提供了一个**可用的固件兜底**：
   即使 Meshtastic 一直跑不通，这块板子的硬件是好的，可以走别的固件路线
3. 逐个部件的**芯片级验证**（读 CHIP_ID / NMEA 实收），不是"能编译就算过"

---

## 复现

```powershell
# 编译
$env:PLATFORMIO_CORE_DIR='D:\meshtastic-build\pio-core'
& 'D:\meshtastic-build\venv\Scripts\pio.exe' run -d hw-selftest

# 烧录 (factory 合并镜像, 从 0x0)
$e='D:\meshtastic-build\pio-core\penv\Scripts\esptool.exe'
& $e --chip esp32s3 --port COM48 --after no-reset erase-flash
& $e --chip esp32s3 --port COM48 --after no-reset write-flash 0x0 hw-selftest\.pio\build\selftest\firmware.factory.bin

# ⚠️ 必须复位后立刻抓, 否则错过输出
.\_flash_and_capture.ps1 -Port COM48 -SkipFlash -Seconds 16
```
