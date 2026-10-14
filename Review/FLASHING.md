# 烧录固件 — T-mesh (ESP32-S3 N8R2 + E22-900M30S)

---

## ⚠️ 先看这一条：插哪个 USB 口

**这块板子上有两个 USB-C，只有一个是数据口。**

| USB-C 位置 | 用途 | 能烧录吗 |
| --- | --- | --- |
| **载板（T-mesh PCB）上的 USB-C** | 接 PD 诱骗模块取 12 V 电 | ❌ **不能**。网表确认它没有 D+/D− 数据线，纯供电 |
| **Pico 模块自己的 USB-C** | CH343P USB 转串口 → ESP32-S3 UART0 | ✅ **就是插这个** |

> 载板那个 USB-C 接的是 `PDC004` PD 诱骗模块，作用是从 PD 充电器协商出 12 V 给整板供电。
> 它和烧录完全无关 —— 插错了电脑上不会出现串口，容易误以为板子坏了。

**Pico 模块的 USB-C 在模块本体上**（不是载板边缘那个）。ESP32-S3-Pico 是插在载板上的邮票孔模块，顺着模块找它自己的 USB-C 座。

---

## 1. 为什么可以全自动烧录（不用按键）

`Datasheet/ESP32-S3-Pico-SCH.pdf` 里已经具备完整条件：

- **CH343P** USB 转串口芯片，接 ESP32-S3 的 **UART0**（`U0TXD` = GPIO43 / `U0RXD` = GPIO44）
- **Q1 / Q2 自动复位电路**：CH343P 的 `DTR` / `RTS` 分别驱动 Q1、Q2，
  再去控制 `CHIP_PU`（EN）和 `GPIO0`
- **SW1** = 复位键，**SW2** = GPIO0（BOOT）

所以 esptool 靠 DTR/RTS 时序就能自动进下载模式。

> 如果自动复位失败（个别情况：串口被占用、线材质量差），**手动来一次**：
> 按住 **SW2** 不放 → 按一下 **SW1** → 松开 SW1 → 松开 SW2 → 再执行烧录命令。
> 手动进下载模式后，esptool 要加 `--before no_reset`，否则它会再复位一次把板子踢出下载模式。

---

## 2. 一次性的准备工作

```powershell
# 查找 Pico 的串口（插上 Pico 的 USB-C 后执行）
Get-CimInstance Win32_PnPEntity |
  Where-Object { $_.Name -match 'COM\d+' } |
  Select-Object Name, DeviceID | Format-Table -AutoSize
```

期望看到一个类似 **`USB-Enhanced-SERIAL CH343 (COM5)`** 的设备。
**记下这个 COM 号**，下面要用。

> 如果什么都没出现：
> - 换一根**能传数据**的 USB 线（很多线是纯充电线）
> - 确认插的是 Pico 模块的 USB-C
> - Windows 可能需要 CH343 驱动（`WCH` 的 CH34x 驱动）

### 准备 esptool

**最省事的办法：直接用 PlatformIO 自带的那份**（装过 PlatformIO 就一定存在）：

```powershell
# 确认能用
& 'D:\meshtastic-build\pio-core\penv\Scripts\esptool.exe' version
```

如果你的 PlatformIO 装在别处，把路径换成你自己的 `pio-core\penv\Scripts\esptool.exe`。

也可以单独装一份：

```powershell
py -3.12 -m pip install esptool
# 之后用:  python -m esptool ...
```

> 下文示例统一写成 `esptool`，实际执行时替换成上面任一种调用方式。
> 如果你用 `Review/flash.ps1`，它会自动找到，不用管这些。

---

## 3. ⚠️ 首次烧录前必须整片擦除

这一版固件用的是 **N8R2 的 8 MB 分区表**（`default_8MB.csv`）。

**如果你之前刷过按 16 MB 布局的构建**（比如我早期那版，或者直接用了默认的
`esp32-s3-pico` 板级配置），分区地址变了 —— NVS 区不在原来的位置。
不擦除直接烧，会出现：

- 已保存的 Meshtastic 配置读到垃圾数据
- 节点 ID 异常 / 反复重启 / 配置改不动

```powershell
# 整片擦除（把 COM5 换成你的实际端口）
python -m esptool --chip esp32s3 --port COM5 erase-flash
```

> **什么时候不用擦**：全新没刷过的芯片，或者你之前刷的就是 8 MB 布局的构建。
> 拿不准就擦 —— 代价只是配置要重新设一遍。

---

## 4. 烧录

仓库里 `Review/firmware/` 有三个镜像：

| 文件 | 用途 |
| --- | --- |
| `tmesh-s3-e22-factory.bin` | **合并镜像，从 `0x0` 烧**。首次烧录 / 擦除后用这个 |
| `tmesh-s3-e22-app.bin` | 仅应用镜像，**从 `0x10000` 烧**。只更新程序时用 |
| `tmesh-s3-e22-littlefs.bin` | 文件系统镜像 |

### 方式 A：factory 镜像（推荐，最简单）

```powershell
python -m esptool --chip esp32s3 --port COM5 --baud 921600 `
  write-flash 0x0 "Review\firmware\tmesh-s3-e22-factory.bin"
```

一个文件搞定 bootloader + 分区表 + 应用，不用管各自的偏移。

### 方式 B：只更新应用（保留配置，最常用）

```powershell
python -m esptool --chip esp32s3 --port COM5 --baud 921600 `
  write-flash 0x10000 "Review\firmware\tmesh-s3-e22-app.bin"
```

### 方式 C：用辅助脚本

仓库里带了 `Review/flash.ps1`，会自动找 COM 口和 esptool：

```powershell
# 自动检测端口 + 整片擦除 + 烧 factory
.\Review\flash.ps1 -Erase

# 指定端口，只更新应用
.\Review\flash.ps1 -Port COM5 -Mode app
```

---

## 5. 如果用 PlatformIO 构建（集成烧录）

```powershell
$env:PLATFORMIO_CORE_DIR='D:\meshtastic-build\pio-core'
& 'D:\meshtastic-build\venv\Scripts\pio.exe' run -e tmesh-s3-e22 -t upload -d 'D:\meshtastic-build\firmware'
```

PlatformIO 会自己对 `board_upload.flash_size = 8MB` 和 `esptool` 参数做处理。
但**它不会替你擦除** —— 分区表变化的那一次，仍然要先手动 `erase-flash`。

---

## 6. 烧录后的第一个判断点

接串口看日志（115200）：

```powershell
# 用 PlatformIO
& 'D:\meshtastic-build\venv\Scripts\pio.exe' device monitor -e tmesh-s3-e22 -d 'D:\meshtastic-build\firmware'
```

**要找的这一行**：

```
Set DIO2 as RF switch, result: 0
```

- 出现且 `result: 0` → `SX126X_DIO2_AS_RF_SWITCH` 生效，射频开关配置正确 ✅
- **没出现，或 result 非 0** → 射频开关没配上，收发都会废

紧接着应该看到射频初始化成功、开始收包。其它要验证的项见
`Review/MESHTASTIC-VARIANT.md` 第 5 节（U4 跳线、发射电流、RX 灵敏度、I2C 设备扫描）。

---

## 7. 常见问题

| 现象 | 原因 / 处理 |
| --- | --- |
| PC 上完全不出现串口 | ① 插错口了（插到载板的 USB-C）② USB 线是纯充电线 ③ 缺 CH34x 驱动 |
| **出现多个串口，不确定用哪个** | 见下方"怎么确定是哪个口" |
| `Failed to connect to ESP32-S3: No serial data received` | 自动复位没成功，或插的是别的设备 → 见下方排查 |
| 报 `This chip is ESP32-C3, not ESP32-S3` | **插错设备了**。这个口上是另一颗芯片，不是本板 |
| `The chip is busy` / 串口被占用 | 关掉串口监视器 / Meshtastic 客户端 / 其它串口工具，再试 |
| 烧完起不来、反复重启 | 分区表不匹配 → 执行第 3 节的 `erase-flash` 后重烧 factory |
| 能烧进去但收不到包 | 查 U4 跳线是否短接（见 `HARDWARE-REVIEW.md` 第 1 节） |

### 怎么确定是哪个口

插拔法最可靠：

```powershell
# 1. 先列出当前所有串口
[System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object

# 2. 拔掉 Pico 的 USB-C

# 3. 再列一次，消失的那个就是它
[System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
```

或者直接让 esptool 挨个试（只读 MAC，不写任何东西，安全）：

```powershell
foreach ($p in 'COM11','COM13') {
  & 'D:\meshtastic-build\pio-core\penv\Scripts\esptool.exe' --chip esp32s3 --port $p read-mac
}
```

**认对的特征**：能读出 `Chip is ESP32-S3` 和 MAC 地址的那个口就是本板。
如果报 `This chip is ESP32-C3` 或别的型号，那是另一个设备，换下一个口。

### `No serial data received` 的排查顺序

1. **先确认芯片认对** —— 如果它连"芯片型号"都没报出来，说明这个口上不是 ESP32-S3。
2. **关掉所有占用串口的程序**（串口助手、Meshtastic 客户端、PlatformIO monitor）。
3. **手动进下载模式**：按住 SW2(BOOT) → 点一下 SW1(RESET) → 松开 SW1 → 松开 SW2，
   然后**不要加任何自动复位参数**：
   ```powershell
   & $esptool --chip esp32s3 --port COM11 --before no-reset read-mac
   ```
   这样能确认"手动进下载模式 + 通信"这条路是通的。
4. **降低波特率**：`--baud 115200`。
5. **换 USB 线和 USB 口**（优先主板后置 USB 口，避开前面板和 USB HUB）。

> ⚠️ 注意：ESP32-S3 的原生 USB-Serial-JTAG（GPIO19/20）**不在本板的 40 脚引出**里
> （网表里 GPIO19/GPIO20 未使用）。所以**不能指望原生 USB 下载**，
> 只能走 Pico 上的 CH343P + UART0 这条路。

---

## 8. 本节结论来源

不是照抄通用教程，是从这块板的实际设计文件核对出来的：

| 结论 | 依据 |
| --- | --- |
| 载板 USB-C 无数据线，纯供电 | `FlyingProbeTesting.json` 全部 386 焊盘的网络名里**没有任何 USB 数据网络**（D+/D−） |
| 载板 USB-C 接 PD 诱骗 | 网表 `$1N26 = {PD1.2, D1.1}`，PD1 = `PDC004-PD诱骗模块` |
| Pico 有 CH343P + 自动复位 | `Datasheet/ESP32-S3-Pico-SCH.pdf`：`CH343P`、`Q1`(DTR→…)、`Q2`(RTS→…)、`SW1`、`SW2` |
| CH343P 接 UART0 | 原理图 `CH_TXD`/`CH_RXD` → `U0RXD`/`U0TXD`（GPIO44 / GPIO43） |
| 必须用 8 MB 分区表 | 板子是 ESP32-S3 **N8R2**；`boards/esp32-s3-pico.json` 默认写的是 16 MB |
