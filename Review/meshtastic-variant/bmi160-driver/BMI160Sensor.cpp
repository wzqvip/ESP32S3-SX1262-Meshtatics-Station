// ============================================================================
// BMI160 加速度计驱动 (Bosch) —— 为 T-mesh 增加
// ============================================================================
// 从 datasheet (BST-BMI160-DS000-07) 核对的寄存器与取值。
// 不依赖外部库, 纯 I2C 寄存器访问, 风格对齐 src/motion/BMI270Sensor.cpp。
// ============================================================================

#include "BMI160Sensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_BMI160)

#include "detect/ScanI2CTwoWire.h"

// ---- 寄存器 (BMI160 datasheet Table: Register map) ----
#define BMI160_REG_CHIP_ID 0x00       // 固定 0xD1
#define BMI160_REG_ACC_X_L 0x12       // 0x12..0x17 = acc X/Y/Z 各 16bit 小端
#define BMI160_REG_ACC_CONF 0x40      // bit7..4 = BWP, bit3..0 = ODR
#define BMI160_REG_ACC_RANGE 0x41     // bit3..0: 0x03=±2g 0x05=±4g 0x08=±8g 0x0C=±16g
#define BMI160_REG_CMD 0x7E           // 命令寄存器
#define BMI160_REG_ACC_PWR_CTRL 0x7D  // 0x04 = acc 使能
// 注意: BMI160【没有】BMI270 那个 PWR_CONF(0x7C) 寄存器, 别照抄 BMI270。

#define BMI160_CHIP_ID_VALUE 0xD1

// 命令字
#define BMI160_CMD_SOFTRESET 0xB6
#define BMI160_CMD_ACC_NORMAL 0x11  // acc 进入 normal mode (开始出数据)
#define BMI160_CMD_ACC_SUSPEND 0x10

// 加速度计配置
//   BWP (bit7..4): 0x2 = "normal" 平均 4 个采样
//   ODR (bit3..0): 0x8 = 100Hz
// 100Hz 比 BMI270 那里的 50Hz 略快, 让运动检测更灵敏; 轮询仍是 50ms 一次。
#define BMI160_ACC_BWP_NORMAL 0x20
#define BMI160_ACC_ODR_100HZ 0x08
#define BMI160_ACC_RANGE_2G 0x03  // ±2g
#define BMI160_ACC_PWR_ENABLE 0x04

// 判定为"动了"的阈值。±2g 时 1g = 16384 LSB, 所以 3200 ≈ 0.2g
// (与 BMI270 驱动保持一致的量级)
#define BMI160_MOTION_THRESHOLD 3200

BMI160Sensor::BMI160Sensor(ScanI2C::FoundDevice foundDevice) : MotionSensor::MotionSensor(foundDevice)
{
    if (foundDevice.address.port == ScanI2C::I2CPort::WIRE1) {
        // 交给扫描器解析: WIRE1 可能是桥接总线而非本地 Wire1
        wire = ScanI2CTwoWire::fetchI2CBus(foundDevice.address);
    } else {
        wire = &Wire;
    }
}

bool BMI160Sensor::writeRegister(uint8_t reg, uint8_t value)
{
    wire->beginTransmission(deviceAddress());
    wire->write(reg);
    wire->write(value);
    return wire->endTransmission() == 0;
}

uint8_t BMI160Sensor::readRegister(uint8_t reg)
{
    wire->beginTransmission(deviceAddress());
    wire->write(reg);
    wire->endTransmission(false);
    wire->requestFrom(deviceAddress(), (uint8_t)1);
    if (wire->available()) {
        return wire->read();
    }
    return 0;
}

bool BMI160Sensor::readRegisters(uint8_t reg, uint8_t *data, size_t len)
{
    wire->beginTransmission(deviceAddress());
    wire->write(reg);
    wire->endTransmission(false);
    size_t bytesRead = wire->requestFrom(deviceAddress(), (uint8_t)len);
    if (bytesRead != len) {
        // 把残留字节读掉, 保持总线状态干净, 然后上报失败
        for (size_t i = 0; i < bytesRead && wire->available(); i++) {
            data[i] = wire->read();
        }
        return false;
    }
    for (size_t i = 0; i < len && wire->available(); i++) {
        data[i] = wire->read();
    }
    return true;
}

bool BMI160Sensor::init()
{
    delay(10);

    // 1) 先确认芯片在, 再动手 (避免对非 BMI160 的 0x68 器件乱写寄存器)
    uint8_t chipId = readRegister(BMI160_REG_CHIP_ID);
    if (chipId != BMI160_CHIP_ID_VALUE) {
        LOG_WARN("BMI160: unexpected CHIP_ID 0x%02X (expected 0xD1)", chipId);
        return false;
    }

    // 2) 软复位
    writeRegister(BMI160_REG_CMD, BMI160_CMD_SOFTRESET);
    delay(50);

    // 复位后 chip id 应重新可读
    if (readRegister(BMI160_REG_CHIP_ID) != BMI160_CHIP_ID_VALUE) {
        LOG_WARN("BMI160: chip id lost after softreset");
        return false;
    }

    // 3) 加速度计配置: 100Hz / normal BWP / ±2g
    if (!writeRegister(BMI160_REG_ACC_CONF, BMI160_ACC_BWP_NORMAL | BMI160_ACC_ODR_100HZ)) {
        LOG_WARN("BMI160: ACC_CONF write failed");
        return false;
    }
    if (!writeRegister(BMI160_REG_ACC_RANGE, BMI160_ACC_RANGE_2G)) {
        LOG_WARN("BMI160: ACC_RANGE write failed");
        return false;
    }

    // 4) 使能加速度计
    if (!writeRegister(BMI160_REG_ACC_PWR_CTRL, BMI160_ACC_PWR_ENABLE)) {
        LOG_WARN("BMI160: ACC_PWR_CTRL write failed");
        return false;
    }
    delay(5);

    // 5) 切到 normal mode —— 之后才开始产出数据
    //    (BMI270 是靠 PWR_CTRL 直接使能, BMI160 必须走 CMD 寄存器)
    if (!writeRegister(BMI160_REG_CMD, BMI160_CMD_ACC_NORMAL)) {
        LOG_WARN("BMI160: ACC_NORMAL_MODE cmd failed");
        return false;
    }
    // 切换模式需要时间, datasheet 建议 ~3.2ms(acc), 这里给足
    delay(60);

    // 6) 数据就绪吗? (用一次真实读取来验证, 而不是只看 ACK)
    uint8_t probe[6];
    if (!readRegisters(BMI160_REG_ACC_X_L, probe, sizeof(probe))) {
        LOG_WARN("BMI160: acc data read failed during init");
        return false;
    }

    initialized = true;
    return true;
}

int32_t BMI160Sensor::runOnce()
{
    if (!initialized) {
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

    // 读 6 字节加速度 (X/Y/Z, 各 16bit 小端)
    uint8_t data[6];
    if (!readRegisters(BMI160_REG_ACC_X_L, data, sizeof(data))) {
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

    int16_t x = (int16_t)((data[1] << 8) | data[0]);
    int16_t y = (int16_t)((data[3] << 8) | data[2]);
    int16_t z = (int16_t)((data[5] << 8) | data[4]);

    if (!hasBaseline) {
        prevX = x;
        prevY = y;
        prevZ = z;
        hasBaseline = true;
        return MOTION_SENSOR_CHECK_INTERVAL_MS;
    }

    // 与上一采样的差值 —— 静止时接近 0, 动起来会明显跳
    int16_t deltaX = abs(x - prevX);
    int16_t deltaY = abs(y - prevY);
    int16_t deltaZ = abs(z - prevZ);

    // 用低通更新基线, 避免缓慢倾斜被误判成"运动"
    prevX = (prevX * 9 + x) / 10;
    prevY = (prevY * 9 + y) / 10;
    prevZ = (prevZ * 9 + z) / 10;

    if (deltaX > BMI160_MOTION_THRESHOLD || deltaY > BMI160_MOTION_THRESHOLD ||
        deltaZ > BMI160_MOTION_THRESHOLD) {
        wakeScreen();
        return 500; // 运动后短时间内更密集地采样
    }

    return MOTION_SENSOR_CHECK_INTERVAL_MS;
}

#endif
