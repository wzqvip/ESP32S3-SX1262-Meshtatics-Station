# ⚠️ 已解决：PSRAM 启动崩溃的根因与解法

> **状态：已解决。Meshtastic 成功启动并运行。**
> 结论先写在这里，排查过程见文末。

---

## 1. 根因

**板级 JSON 里的 PSRAM 声明会让框架的板级配置生成器强制覆盖 `sdkconfig`。**

`pio-core/platforms/espressif32/builder/frameworks/espidf.py` 的
`generate_board_specific_config()` 会：

1. 检查板级 JSON 是否声明 PSRAM（`extra_flags` 里的 `-DBOARD_HAS_PSRAM`，
   或 `build.psram_type` / `build.arduino.memory_type`）
2. 若声明了，就**强制写入** `CONFIG_SPIRAM=y` + `CONFIG_SPIRAM_MODE_*`

构建日志里能直接看到它在"改回"：

```
Replace: CONFIG_SPIRAM=y with: CONFIG_SPIRAM=y          ← 即使我设成 n，也被改回 y
Add: # meshtastic_hybridcompile_cache_key: memory_type=qio_qspi spiram=quad
```

而 **bootloader 阶段**（`E (407)`，上电 407 µs）会去初始化 PSRAM，
失败就 panic —— 这就是 `octal_psram: PSRAM chip is not connected` 的来源。

### 为什么之前 4 种改法都无效

| 改哪里 | 为什么无效 |
| --- | --- |
| `platformio.ini` 的 `memory_type` | `espidf.py:3089` 只读**板级 JSON** 的 `memory_type` |
| `build_unflags -D BOARD_HAS_PSRAM` | 只影响应用层宏，改不到 IDF sdkconfig |
| `sdkconfig.defaults` 设 `CONFIG_SPIRAM=n` | 被板级配置生成器**覆盖** |
| 自建板级 JSON 但**照抄了** `BOARD_HAS_PSRAM` | 等于没改，PSRAM 照样被强制打开 |

**真正的开关只有一个：板级 JSON 里不要出现任何 PSRAM 声明。**

---

## 2. 解法

### `boards/tmesh-s3-e22.json`（关键：不含 PSRAM 字段）

```json
{
  "build": {
    "arduino": {
      "ldscript": "esp32s3_out.ld",
      "partitions": "default_8MB.csv"
    },
    "core": "esp32",
    "extra_flags": [
      "-DARDUINO_ESP32S3_DEV",
      "-DARDUINO_RUNNING_CORE=1",
      "-DARDUINO_EVENT_RUNNING_CORE=1",
      "-DARDUINO_USB_CDC_ON_BOOT=1"
    ],
    "f_cpu": "240000000L",
    "f_flash": "80000000L",
    "flash_mode": "qio",
    "mcu": "esp32s3",
    "variant": "esp32s3"
  },
  ...
}
```

**必须去掉的三项**（这是原来从 `esp32-s3-pico.json` 抄过来的）：

- ❌ `extra_flags` 里的 `"-DBOARD_HAS_PSRAM"`
- ❌ `"psram_type": "qspi"`
- ❌ `build.arduino` 里的 `"memory_type": "qio_qspi"`

### 验证标志

构建日志必须出现这两行（对比之前是 `spiram=quad`）：

```
Replace: CONFIG_SPIRAM=y with: # CONFIG_SPIRAM is not set
Add: # meshtastic_hybridcompile_cache_key: memory_type=qio_qspi spiram=none
```

`spiram=none` = 成功。

---

## 3. 为什么这样行 —— 关键认识

**PSRAM 由两部分独立处理：**

| 阶段 | 由谁初始化 | 结果 |
| --- | --- | --- |
| **bootloader**（`E 407`） | IDF sdkconfig（`CONFIG_SPIRAM`） | ❌ 关掉后不再尝试 → **不崩了** |
| **应用运行时** | Arduino core / Meshtastic 自己的分配器 | ✅ **依然能用 PSRAM** |

所以关掉 `CONFIG_SPIRAM` **不等于**失去 PSRAM。启动后 Meshtastic 仍然报告：

```
DEBUG | Total PSRAM: 2097152
DEBUG | Free PSRAM: 2089140
```

**而 bootloader 不再去碰它，就不会崩。**

> 这也解释了为什么硬件自检（`hw-selftest/`）能成功：它用的是干净框架，
> PSRAM 由应用层初始化，从来不经过 Meshtastic 那个"强制 PSRAM 的 bootloader"。

---

## 4. 成功启动的证据（实测日志）

```
//\ E S H T /\ S T / C
INFO  | S:B:255,2.8.2.04cb888,tmesh-s3-e22,meshtastic/firmware
DEBUG | Total PSRAM: 2097152
DEBUG | Free PSRAM: 2089140
DEBUG | Use nodenum 0x63a6ead4              ← 由 MAC 派生, 与 e4:b0:63:a6:ea:d4 一致
INFO  | BMP-280 found at address 0x76
DEBUG | SPI.begin(SCK=12, MISO=13, MOSI=11, NSS=10)     ← 引脚全对
DEBUG | SX126xInterface(cs=10, irq=1, rst=5, busy=4)    ← 引脚全对
DEBUG | SX126X_DIO3_TCXO_VOLTAGE defined, DIO3 as TCXO Vref 1.800000 V
INFO  | Radio freq=906.875
INFO  | Requested Tx power: 30 dBm; Device LoRa Tx gain: 7 dB   ← EBYTE_E22_900M30S 生效
INFO  | Final Tx power: 15 dBm                                  ← TMESH_LOW_POWER_TEST 生效
INFO  | SX126x init result 0
INFO  | SX1262 init success
DEBUG | Set DIO2 as RF switch, result: 0        ★ 本板核心配置生效
DEBUG | SX126X_RXEN not defined, default RADIOLIB_NC  ★ 确认没去碰不存在的 GPIO
DEBUG | SX126X_TXEN not defined, default RADIOLIB_NC
DEBUG | Use MCU pin -1 as RXEN, pin -1 as TXEN        ★ MCU 不控制射频开关
INFO  | Applied SX1262 reg 0x8B5 RX patch
INFO  | [PowerFSM] BLE Advertising started
DEBUG | Node status update: 1 online, 1 total
```

### 逐条确认

| 项 | 状态 |
| --- | --- |
| 启动 | ✅ 完整启动，无崩溃 |
| 射频初始化 | ✅ `SX126x init result 0` |
| **DIO2 射频开关** | ✅ `result: 0` —— 本板最关键的一项 |
| 未误配 TXEN/RXEN | ✅ 两个都 `RADIOLIB_NC`，符合"MCU 不参与"的设计 |
| 发射功率映射 | ✅ `Tx gain 7 dB`，`Final Tx power 15 dBm` |
| SPI 引脚 | ✅ SCK12/MISO13/MOSI11/NSS10 |
| 控制引脚 | ✅ cs10/irq1/rst5/busy4 |
| TCXO | ✅ DIO3 @ 1.8V |
| PSRAM | ✅ 运行时可用 2MB |
| BLE | ✅ 已广播 |
| 频段 | ✅ 906.875 MHz (US 902-928) |

### 上板后仍待办

- [ ] `region=UNSET` —— 需要在 App / CLI 里设置区域（美国选 US）
- [ ] 放开 1W：注释掉 `platformio.ini` 里的 `-D TMESH_LOW_POWER_TEST`
- [ ] OLED 未检测到（自检也报无应答，查 `U15` 供电焊桥与模块 pin 顺序）
- [ ] BMI160 / PCF8563 在 Meshtastic 侧未初始化（自检确认硬件正常，属配置未启用）
- [ ] 实测收发包、发射电流、GPS 定位

---

## 5. 完整排查历程（避开这些弯路）

| 阶段 | 做法 | 结果 |
| --- | --- | --- |
| 1 | 初版按板级 JSON 默认 `qio_opi` | ❌ 崩 |
| 2 | 看到 `octal_psram` 报错，推断"芯片是 Octal"，改 `qio_opi` | ❌ 崩（**推断错误**） |
| 3 | 依据原理图判定是 Quad，改 `qio_qspi` | ❌ 崩 |
| 4 | `build_unflags` 去掉 `BOARD_HAS_PSRAM` | ❌ 崩（改不到 sdkconfig） |
| 5 | 自建板级 JSON 写 `memory_type: qio_qspi` | ❌ 崩（**照抄了 PSRAM 字段**） |
| 6 | 改 `sdkconfig.defaults` 设 `CONFIG_SPIRAM=n` | ❌ 崩（**被板级生成器覆盖**） |
| 7 | **硬件自检工程（独立于 Meshtastic）** | ✅ **证明 PSRAM 硬件可用** |
| 8 | **自建 JSON 去掉全部 PSRAM 声明** | ✅ **成功启动** |

### 两个关键教训

1. **不要用报错文本反推硬件。** 我看到 `octal_psram` 就断定芯片是 Octal，
   实际上那只是 IDF 驱动模块的函数名，Quad 配置也走同一段代码。
   **要看原理图。**（后来靠设计者提供的模块原理图才纠正）

2. **改配置前先确认它会不会被覆盖。** Meshtastic/pioarduino 的板级配置生成器
   会**反向覆盖** `sdkconfig`，所以 `sdkconfig.defaults` 不是最终权威；
   最终权威是**板级 JSON**。

### 硬件自检的价值

`hw-selftest/` 这个独立工程是破局点 —— 它证明了：

- PSRAM 硬件完全可用（2MB + 64KB 读写一致）
- Flash 8MB 正确
- I2C 上 BMI160(`CHIP_ID=0xD1`) / BMP280(`ID=0x58`) / PCF8563 都正常
- GPS 实收 51 条 NMEA
- GPIO 空闲脚与网表一致

详见 `HARDWARE-SELFTEST.md`。

---

## 6. 复现命令

```powershell
$env:PLATFORMIO_CORE_DIR='D:\meshtastic-build\pio-core'

# 全量清理 (板级配置变了, 必须清)
Remove-Item D:\meshtastic-build\firmware\.pio\build\tmesh-s3-e22 -Recurse -Force

# 编译 (日志里应出现 spiram=none)
& 'D:\meshtastic-build\venv\Scripts\pio.exe' run -e tmesh-s3-e22 -d 'D:\meshtastic-build\firmware'

# 烧录
.\Review\flash.ps1 -Port COM48 -Erase -Mode factory

# 抓日志 (必须复位后立刻抓)
.\_flash_and_capture.ps1 -Port COM48 -SkipFlash -Seconds 18
```
