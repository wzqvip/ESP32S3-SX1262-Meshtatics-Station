# 当前状态与已知取舍

> 更新于本板调试基本完成时。Meshtastic 已能完整启动运行。

---

## ⚠️ 一个反复踩到的坑：`.elf` 更新了但 `.bin` 没更新

调试过程中被这个坑耽误过两次，**务必注意**。

### 现象

```
firmware-...-2.8.2.04cb888.elf          15:59:48     ← 重新链接了
firmware-...-2.8.2.04cb888.bin          15:49:33     ← 没更新!
firmware-...-2.8.2.04cb888.factory.bin  15:49:33     ← 没更新!
```

PlatformIO 有时**只重新链接 `.elf`，不重新生成可烧录的 `.bin`**。
结果是：编译日志显示 `SUCCESS`，`.elf` 里确实有新代码，
但 `.bin` 还是旧的 —— 烧进去等于没改。

### 排查方法（每次烧录前做）

**1. 检查时间戳，别只看 SUCCESS：**

```powershell
Get-ChildItem .pio\build\tmesh-s3-e22 -Filter 'firmware*' |
  Sort-Object LastWriteTime -Descending |
  Select-Object Name, Length, LastWriteTime
```

`.bin` 的时间必须**不早于** `.elf`，也不早于你最近一次改的源文件。

**2. 从 `.elf` 里查特征字符串**，确认改动真的编进去了：

```powershell
# 例: 确认 BUTTON_PIN 生效 —— 这两条欢迎文案是互斥的
python -c "
d=open('.pio/build/tmesh-s3-e22/firmware-....elf','rb').read()
print('new:', d.count(b'Click to navigate and'))        # 需要 BUTTON_PIN -> 应为 1
print('old:', d.count(b'Use the Select button'))        # 无 BUTTON_PIN -> 应为 0
"
```

注意：**`.bin` 是压缩的，不能直接搜字符串，要搜 `.elf`。**

**3. 如果 `.bin` 没更新，强制全量重建：**

```powershell
pio run -e tmesh-s3-e22 -t clean
pio run -e tmesh-s3-e22
```

---

## ✅ 已跑通

| 项 | 状态 | 证据 |
| --- | --- | --- |
| 启动 | ✅ | 完整启动，无重启循环 |
| 串口 | ✅ | ESP32-S3 原生 USB (`COM48`) |
| **射频初始化** | ✅ | `SX126x init result 0` / `SX1262 init success` |
| **DIO2 射频开关** | ✅ | `Set DIO2 as RF switch, result: 0` |
| 未误配 TXEN/RXEN | ✅ | 两者都 `RADIOLIB_NC` |
| **发射功率映射** | ✅ | `Tx gain 7 dB` → `Final Tx power 15 dBm` |
| 屏幕 | ✅ | `Turn on screen` / `[Screen] numframes: 8` |
| **按钮 (BOOT/GPIO0)** | ✅ **实测通过** | **单击切界面、长按开菜单**（设计者实测） |
| BLE | ✅ | `BLE Advertising started` |
| GPS | ✅ | 自检实收 51 条 NMEA |
| 传感器 | ✅ | BMP280 `0x76` 已识别；I2C 三个器件自检通过 |
| 频段 | ✅ | 906.875 MHz (US) |

### 按钮映射（单击 / 长按）

`src/input/InputBroker.cpp` 有 screen 的分支：

```c
userConfig.singlePress   = INPUT_BROKER_USER_PRESS;   // 单击 = 切换界面帧
userConfig.longPress     = INPUT_BROKER_SELECT;       // 长按 500ms = 打开菜单/选择
userConfig.longPressTime = 500;
```

与开箱横幅 `"Click to navigate and long press to select or open a menu."` 一致。

> **注意：Meshtastic 默认不为单击打日志。**
> `ButtonThread.cpp` 里只有双击（`LOG_INFO("Double press")`）和
> 连击（`LOG_INFO("Multipress!")`）会打日志，单击是静默的。
> 所以"日志里没有按键事件"**不能**说明按钮没工作 —— 要看屏幕反应。


---

## ⚠️ 已知取舍：PSRAM 被关闭带来的内存减少

### 现象

为了让 bootloader 不崩，板级 JSON 里去掉了 PSRAM 声明
（详见 `PSRAM-FIX-SOLVED.md`）。这带来一个副作用：

```
之前(PSRAM 启用):  Free PSRAM: 2089140
现在(PSRAM 关闭):  Free PSRAM: 0

[TM] Allocating unified cache: 2048 entries (20480 bytes)   ← 之前
[TM] Allocating unified cache:  500 entries ( 5000 bytes)   ← 现在

[TM] NodeInfo cache: 2000 entries, 248000 bytes (PSRAM)     ← 之前
[TM] NodeInfo cache not available on this target            ← 现在
```

### 影响

- **TrafficManagement 的统一缓存**从 2048 条降到 500 条
- **NodeInfo 缓存**（2000 条 / 248 KB）不可用
- 内部堆约 124–238 KB

对**小规模自组网 / 基站**够用；但如果是**节点很多、流量大的 mesh**，
缓存变小会影响去重与节点信息保留效果。

### 为什么不能两全

矛盾在于：

| 阶段 | 由谁控制 | 我们的处境 |
| --- | --- | --- |
| **bootloader** 初始化 PSRAM | IDF `CONFIG_SPIRAM` | 一旦打开 → 崩 |
| **应用运行时** 用 PSRAM | Arduino/Meshtastic | 打开 `CONFIG_SPIRAM` 才有 |

而 Meshtastic 的板级配置生成器（`espidf.py::generate_board_specific_config`）
只要看到板级 JSON 声明了 PSRAM，就**强制打开** `CONFIG_SPIRAM` ——
**无法把这两者分开**。

### 后续可选尝试（未验证）

如果确实需要 PSRAM 那 2 MB，可以试：

1. **换 PSRAM 速度**：在 sdkconfig 里试 `CONFIG_SPIRAM_SPEED_40M`（当前 80M）。
   ESP32-S3 在 **DIO flash + 80MHz** 下配合 PSRAM 时序有时不稳；
   ROM 日志里确实报的是 `mode:DIO`，而 sdkconfig 写的是 `CONFIG_FLASHMODE_QIO` ——
   **这个不一致还没排除**。
2. **明确指定 PSRAM 类型**：试 `CONFIG_SPIRAM_TYPE_ESPPSRAM16/32/64`
   而不是 `TYPE_AUTO`。
3. **试 Octal 路径**：`CONFIG_SPIRAM_MODE_OCT=y`（虽然原理图判断是 Quad，
   但报错文本一直是 `octal_psram`）。
4. **在 Meshtastic 上游提 issue**：板级生成器强制打开 PSRAM 与"M8 无屏幕分支
   长按映射为 SHUTDOWN"这两点，都值得反馈。

> 试这些之前请先备份当前**可运行**的构建配置。

---

## 📌 上板后仍待办

- [x] ~~**确认按钮手感**~~ → **已解决**：单击切界面 / 长按开菜单，实测通过（不需要额外加按钮）
- [ ] **设置区域**：日志里是 `region=UNSET`，需要在 App / CLI 里选 US 才能真正收发
- [ ] **实测收发包**：两台设备对通，看 `rx_snr` / `rx_rssi`
- [ ] **放开 1W**：注释掉 `platformio.ini` 里的 `-D TMESH_LOW_POWER_TEST`
      （当前封在 15 dBm ≈ 32 mW）
- [ ] **OLED**：设计者说明还没焊接，等焊好后再测（自检报无应答属正常）
- [ ] **BMI160 / PCF8563**：硬件自检已确认正常，Meshtastic 侧需在配置里启用
- [ ] **发射电流**：1W 时整机应有约 +650 mA 跳升

---

## 交付物索引

| 文件 | 内容 |
| --- | --- |
| `PSRAM-FIX-SOLVED.md` | PSRAM 崩溃的根因与解法（核心） |
| `HARDWARE-SELFTEST.md` | 硬件自检结果（PSRAM 可用性证明） |
| `MESHTASTIC-VARIANT.md` | Meshtastic 变体适配说明、功率映射、编译验证 |
| `FLASHING.md` / `flash.ps1` | 烧录流程与脚本 |
| `HARDWARE-REVIEW.md` | 载板硬件复核 |
| `NANO-G1-VS-THIS-BOARD.md` | 与 Nano G1 Explorer 的差异 |
| `hardware-schematic/` | 复制版 Pico 模块原理图 |
| `meshtastic-variant/` | 变体源码 + 板级 JSON |
| `hw-selftest/`（仓库根） | 独立硬件自检工程 |
