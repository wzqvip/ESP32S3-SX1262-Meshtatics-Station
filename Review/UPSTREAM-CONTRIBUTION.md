# 如何把改动推给 Meshtastic 上游

> 回答"要不要先 fork 一个" —— **要**，但更重要的是先想清楚：
> **哪些改动该进上游，哪些应该留在你自己的 fork 里。**
> 两者混在一起提交，PR 大概率会被要求拆分。

---

## 0. 先分类：你这堆改动是两类东西

| 改动 | 性质 | 该去哪 |
| --- | --- | --- |
| `BMI160Sensor.{h,cpp}` | **通用驱动**，任何接 BMI160 的板子都能用 | ✅ **上游** |
| 枚举 + 探测支持（`ScanI2C.*`、`AccelerometerThread.h`） | 通用基础设施 | ✅ **上游** |
| `variants/esp32s3/diy/tmesh-s3-e22/` | **板级变体** | ✅ 上游（走独立 PR） |
| `boards/tmesh-s3-e22.json` | 板级定义 | ✅ 上游（跟变体一起） |
| `Review/`、`tools/`、自检工程 | 你仓库的文档与工具 | ❌ 留自己仓库 |
| 本地 `configuration.h` / `SX126xInterface.cpp` 的断言补丁 | 板级调试用 | ⚠️ 见下 |

**建议分两个 PR**，不要合成一个：

1. **PR #1：BMI160 驱动 + 探测支持**（纯通用功能，最容易通过）
2. **PR #2：T-mesh 板级变体**（需要硬件照片、引脚表等，审核更久）

> ⚠️ **本地那两个上游补丁要注意**：
> `configuration.h` 里的 `TMESH_LOW_POWER_TEST`、`SX126xInterface.cpp` 里的
> `#error` 断言，都是**你板子专用的调试代码**。提 PR 时**不要**带上，
> 否则会污染公共路径。变体该用 `variant.h` 里的宏表达，而不是改公共文件。

---

## 1. 前置：必须签 CLA（最容易卡住的一步）

Meshtastic 官方 CONTRIBUTING 明确写了：

> Before making any contributions, you must sign our Contributor License
> Agreement (CLA). You can do this by visiting
> <https://cla-assistant.io/meshtastic/firmware>.
> **Be sure to use the GitHub account you will use to submit your contributions.**

**用你打算提 PR 的那个 GitHub 账号去签**，否则 PR 会被卡住不合并。

---

## 2. 然后：开一个 issue / discussion 先说一声

官方希望**先讨论再写代码**，避免重复劳动：

- 新功能/想法 → [GitHub Discussions](https://github.com/meshtastic/firmware/discussions)
- Bug → issue tracker（用他们的模板）
- 实时问题 → [Discord](https://discord.com/invite/ktMAKGBnBs)

对 BMI160 这种"补一个已有家族的缺项"，开个 Discussion 问一句
"要不要加 BMI160 支持"就够，通常很快会有回应。

---

## 3. Fork 与开发流程

### 3.1 Fork + clone

```powershell
# 在 GitHub 网页上点 Fork -> 得到 https://github.com/<你的用户名>/firmware
git clone https://github.com/<你的用户名>/firmware.git D:\meshtastic-fork
cd D:\meshtastic-fork

# 加上游，方便同步
git remote add upstream https://github.com/meshtastic/firmware.git
git fetch upstream
```

### 3.2 从最新的 master 开分支

```powershell
git checkout -b feat/bmi160-driver upstream/master
```

**分支名要有意义**：`feat/bmi160-driver`、`feat/tmesh-s3-e22-variant`。

### 3.3 把改动搬过去

从你现在的仓库（`ESP32S3-SX1262-Meshtatics-Station`）复制：

```powershell
$src = 'C:\Users\WANGZ\Documents\GitHub\ESP32S3-SX1262-Meshtatics-Station\Review\meshtastic-variant'

# --- BMI160 驱动 (PR #1) ---
Copy-Item "$src\bmi160-driver\BMI160Sensor.h"   'D:\meshtastic-fork\src\motion\'
Copy-Item "$src\bmi160-driver\BMI160Sensor.cpp" 'D:\meshtastic-fork\src\motion\'

# 枚举/探测/hread 的改动: 这几个文件已含改动, 但【不要整文件覆盖】
# —— 上游可能已变。请按 Review/BMI160-DRIVER.md 第 2 节的清单手工合并。
```

> ⚠️ **`upstream-modified/` 里的大文件（`ScanI2CTwoWire.cpp` 1257 行、
> `GPS.cpp` 2513 行等）是【改动后的完整副本】，只用于对照参考。**
> 直接覆盖上游文件会连带覆盖掉上游的新提交。**必须手工合并那几处小改动。**

**BMI160 需要手工合并的 4 处**（详见 [BMI160-DRIVER.md](BMI160-DRIVER.md) 第 2 节）：

| 文件 | 改动 |
| --- | --- |
| `src/detect/ScanI2C.h` | 枚举加 `BMI160` |
| `src/detect/ScanI2C.cpp` | `firstAccelerometer()` 列表加 BMI160，计数 +1 |
| `src/detect/ScanI2CTwoWire.cpp` | `0x68/0x69` case 里加 `0xD1` 分支 |
| `src/motion/AccelerometerThread.h` | `#ifdef HAS_BMI160` include + case |

### 3.4 格式化（官方要求）

```powershell
# 官方要求提交前跑 trunk fmt
trunk fmt
```

没装 Trunk 的话，至少保证与 `BMI270Sensor.cpp` 的缩进/风格一致
（本驱动已对齐）。

### 3.5 提交

```powershell
git add src/motion/BMI160Sensor.h src/motion/BMI160Sensor.cpp `
        src/detect/ScanI2C.h src/detect/ScanI2C.cpp `
        src/detect/ScanI2CTwoWire.cpp src/motion/AccelerometerThread.h
git commit -m "Add BMI160 accelerometer support

BMI160 is a Bosch 6-axis IMU widely used on hobby breakout boards. It shares
the 0x68/0x69 addresses with MPU6050/BMI270/BMX160/ICM42607P and is identified
by CHIP_ID 0xD1 at register 0x00.

Unlike BMI270, BMI160 needs no 8KB config blob - two mode-register writes are
enough, so the driver is considerably smaller. Register access is done directly
over I2C with no external library dependency.

Tested on an ESP32-S3 board with a GY-BMI160 module: detection, init, and
motion-triggered screen wake all verified (accel reads ~1g on the resting axis)."
```

### 3.6 推送并开 PR

```powershell
git push -u origin feat/bmi160-driver
```

然后在 GitHub 上开 PR。官方要求：

- 用他们的 **PR 模板** 填描述
- 勾选 **"Allow edits from maintainers"**

---

## 4. 板级变体 PR（PR #2）

变体要进上游，需要一起提供：

| 需要的东西 | 你现在的状态 |
| --- | --- |
| `variants/esp32s3/diy/tmesh-s3-e22/variant.h` | ✅ 有 |
| `variants/esp32s3/diy/tmesh-s3-e22/platformio.ini` | ✅ 有 |
| `boards/tmesh-s3-e22.json` | ✅ 有 |
| 硬件照片 | ⚠️ 要自己拍（板子实物、模块、天线） |
| 引脚表 / 原理图 | ✅ 有（`PCB/`） |
| 说明为何需要独立变体 | 需要写 |

> **注意**：上游对"个人自制板"的接受度有限 —— 数量多的自制板通常
> 要求先有一定社区需求。**建议先把 BMI160 驱动单独提上去**（通用功能，
> 阻力小得多），变体可以留在你自己 fork 里用。

---

## 5. 同步上游（fork 后长期要做的事）

```powershell
cd D:\meshtastic-fork
git fetch upstream
git checkout master
git merge upstream/master          # 或 git rebase upstream/master
git push origin master
```

然后把你自己的分支 rebase 到新的 master：

```powershell
git checkout feat/bmi160-driver
git rebase upstream/master
```

> **为什么不直接用你现在的仓库提 PR**：
> 你的 `ESP32S3-SX1262-Meshtatics-Station` 是一个**项目仓库**，
> 里面混了 PCB、文档、固件二进制、诊断工程。上游只接受**干净的代码改动**。
> 所以 fork `meshtastic/firmware` 并在上面开分支，是必须的。

---

## 6. 检查清单

提 PR 前逐条确认：

- [ ] 已用**提 PR 的账号**签了 [CLA](https://cla-assistant.io/meshtastic/firmware)
- [ ] 在 Discussions 里开过话题（至少提一句）
- [ ] 从 `upstream/master` 开了独立分支
- [ ] **只改了必要文件**，没有把 `Review/`、`tools/`、固件二进制带进去
- [ ] **没有**改动公共调试宏（`TMESH_LOW_POWER_TEST` 之类）
- [ ] 跑过 `trunk fmt`
- [ ] commit message 说清"为什么"和"怎么验证的"
- [ ] PR 描述用了官方模板，并勾选 "Allow edits from maintainers"
- [ ] 在自己的仓库里保留了完整记录（`Review/BMI160-DRIVER.md`）

---

## 7. 你现在的仓库要做什么

**不需要动 fork 相关的东西。** 你的仓库继续作为"项目主仓库"存在，
只需要在 README 里指向本文档，让人知道驱动可以回馈上游。

如果将来驱动被上游合并了，记得在
[BMI160-DRIVER.md](BMI160-DRIVER.md) 顶部加一句
"已合并进上游 <版本>"，并把本地补丁标记为可弃用。
