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

## 1.5 已核实：上游确实没有 BMI160，也没有人在做

> 核查日期 **2026-10-15**。四个独立角度交叉验证，结论一致。

| 检查 | 方法 | 结果 |
| --- | --- | --- |
| `src/motion/BMI160Sensor.{h,cpp}` 是否存在 | GET `raw.githubusercontent.com/.../develop/src/motion/BMI160Sensor.h` | **HTTP 404** |
| 4 个待改文件是否已含 BMI160 | 拉 develop 原文 grep `BMI160` | **各 0 次命中** |
| 是否已有人提 PR / issue | `GET /search/issues?q=BMI160+repo:meshtastic/firmware` | **`total_count: 0`** |
| `src/motion/` 现有 Bosch 家族驱动 | `GET /repos/meshtastic/firmware/contents/src/motion` | 只有 `BMI270` / `BMX160` / `BMM150` / `BMA423` / `BHI260AP`，**无 BMI160** |

**结论：这是个真实缺口，且无人重复劳动。** 可以放心提 PR。

> ⚠️ `develop` 的 `BMI270Sensor.cpp` 有 **56,638 字节**（我本地基线那份才 594 行）
> —— 说明上游在这块代码上很活跃。提交前**务必先 rebase 到最新 develop**
> 再跑一次编译，别用旧基线的结论。

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

### 3.2 从最新的 develop 开分支

> ⚠️ **默认分支是 `develop`，不是 `master`。**
> `master` 是发布分支。开 PR 必须基于 `develop`。
> （依据：GitHub API `repos/meshtastic/firmware` 返回 `"default_branch":"develop"`）

```powershell
git checkout -b feat/bmi160-driver upstream/develop
```

**分支名要有意义**：`feat/bmi160-driver`、`feat/tmesh-s3-e22-variant`。

### 3.3 把改动搬过去

从你现在的仓库（`ESP32S3-SX1262-Meshtatics-Station`）复制：

```powershell
$src = 'C:\Users\WANGZ\Documents\GitHub\ESP32S3-SX1262-Meshtatics-Station\Review\meshtastic-variant'

# --- 2 个全新文件: 直接复制即可 (上游没有, 不会冲突) ---
Copy-Item "$src\bmi160-driver\BMI160Sensor.h"   'D:\meshtastic-fork\src\motion\'
Copy-Item "$src\bmi160-driver\BMI160Sensor.cpp" 'D:\meshtastic-fork\src\motion\'
```

> ⚠️ **`upstream-modified/` 里的大文件（`ScanI2CTwoWire.cpp`、`GPS.cpp` 等）
> 是【基于旧基线 04cb888 的改动后完整副本】，只用于对照参考。**
> 直接覆盖会连带覆盖掉上游的新提交。**另外 4 处必须手工合并。**

#### ⚠️ 基线落后的实情（2026-10-15 核实）

| 项 | 状态 |
| --- | --- |
| 我的构建基线 | `04cb888`（2026-10-13） |
| 上游 `develop` HEAD | `bda9f8c` |
| **落后** | **8 个提交** |
| 我改的 4 个文件中**上游已改过的** | **`ScanI2C.cpp`**、**`ScanI2CTwoWire.cpp`** |

**逐个文件的影响：**

| 文件 | 上游是否已改 | 能否直接套 |
| --- | --- | --- |
| `src/motion/BMI160Sensor.{h,cpp}` | 不存在 | ✅ 直接新增 |
| `src/detect/ScanI2C.h` | 未改动 | ✅ 可干净套 |
| `src/motion/AccelerometerThread.h` | 未改动 | ✅ 可干净套 |
| `src/detect/ScanI2C.cpp` | **已改（+5/−4）** | ⚠️ **需手工合并** |
| `src/detect/ScanI2CTwoWire.cpp` | **已改（+32/−2）** | ✅ 改动在别处，我的插入点未受影响 |

#### 手工合并点 1：`src/detect/ScanI2C.cpp`

上游把 `BMM150` 挪到了列表**末尾**（因为它其实是磁力计）：

```c
// develop 当前内容
// BMM150 is a magnetometer: last, so any real IMU takes the accelerometer thread and leaves it to the magnetometer one
ScanI2C::DeviceType types[] = {MPU6050, LIS3DH,   SC7A20,    BMA423,     LSM6DS3,  BMX160,  STK8BAXX, ICM20948,
                               BMI270,  BHI260AP, ICM42607P, ISM330DHCX, QMA6100P, QMI8658, BMM150};
return firstOfOrNONE(15, types);
```

**要在保留上游排序的前提下加 `BMI160`，并把计数改成 16**：

```diff
 ScanI2C::DeviceType types[] = {MPU6050, LIS3DH,   SC7A20,    BMA423,     LSM6DS3,  BMX160,  STK8BAXX, ICM20948,
-                               BMI270,  BHI260AP, ICM42607P, ISM330DHCX, QMA6100P, QMI8658, BMM150};
-return firstOfOrNONE(15, types);
+                               BMI270,  BMI160,   BHI260AP,  ICM42607P,  ISM330DHCX, QMA6100P, QMI8658, BMM150};
+return firstOfOrNONE(16, types);
```

> **不要**用你本地那份直接覆盖 —— 那会把上游刚做的 BMM150 重排改回去。

#### 手工合并点 2：`src/detect/ScanI2CTwoWire.cpp`

**好消息：上游在这个文件里的改动（`AHT10`/FocalTech 触摸判别、`VEML7700`/`BMM150` 判别）
都在别的位置**（develop 的 541、889 行附近），**没有碰 `0x68` 那个 case**。
所以我的插入点在 develop 上依然有效。

在 `else if (registerValue == 0xD8)`（BMX160）分支**之后**、
最后的 `else {`（0x75 探测 + `probeSEN5X`）**之前**插入：

```c
#if defined(HAS_BMI160)
                else if (registerValue == 0xD1) { // BMI160 chip ID at register 0x00
                    type = BMI160;
                    logFoundDevice("BMI160", (uint8_t)addr.address);
                    break;
                }
#endif
```

> **位置是关键**：必须在 `else` **之前**，否则会先落进 `else`，
> 被里面的 `probeSEN5X()` 抢先匹配成 SEN5X（`SEN5X_ADDR` 也是 0x69，
> 探测在 0x68 上会误报）。详见 [BMI160-DRIVER.md](BMI160-DRIVER.md) 3.2 节。

#### 手工合并点 3 与 4

| 文件 | 改动 |
| --- | --- |
| `src/detect/ScanI2C.h` | 枚举里 `BMI270` 后加 `BMI160` |
| `src/motion/AccelerometerThread.h` | `#ifdef HAS_BMI160` 的 include + `case` 分支 |

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
git checkout develop
git merge upstream/develop
git push origin develop
```

然后把你自己的分支 rebase 到新的 develop：

```powershell
git checkout feat/bmi160-driver
git rebase upstream/develop
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
- [ ] 从 `upstream/develop` 开了独立分支（**不是 master**）
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
