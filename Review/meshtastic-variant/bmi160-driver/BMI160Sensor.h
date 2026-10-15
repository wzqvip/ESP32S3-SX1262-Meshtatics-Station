#pragma once
#ifndef _BMI160_SENSOR_H_
#define _BMI160_SENSOR_H_

// ============================================================================
// BMI160 加速度计驱动 (Bosch) —— 为 T-mesh 增加
// ============================================================================
// 上游没有 BMI160 驱动 (有 BMI270 / BMX160 / BMM150, 但没有 BMI160)。
// 本驱动照 src/motion/BMI270Sensor.* 的风格写成, 但用【原始寄存器访问】,
// 不依赖任何外部库 (Adafruit_BMI160 之类的第三方库不存在/不稳)。
//
// 与 BMI270 的关键区别: BMI160 【不需要上传 8KB 配置块】。
// 只要软复位 -> 设 acc_conf(Odr/Range/Filt) -> 写 cmd=ACC_NORMAL_MODE 即可工作,
// 因此驱动比 BMI270 短得多。
//
// 启用方式: variant.h 里 #define HAS_BMI160
// ============================================================================

#include "MotionSensor.h"

#if !defined(ARCH_STM32WL) && !MESHTASTIC_EXCLUDE_I2C && defined(HAS_BMI160)

class BMI160Sensor : public MotionSensor
{
  private:
    bool initialized = false;
    TwoWire *wire = nullptr;

    // 上一采样值, 用于做差分检测运动
    int16_t prevX = 0, prevY = 0, prevZ = 0;
    bool hasBaseline = false;

    bool writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);
    bool readRegisters(uint8_t reg, uint8_t *data, size_t len);

  public:
    explicit BMI160Sensor(ScanI2C::FoundDevice foundDevice);
    virtual bool init() override;
    virtual int32_t runOnce() override;
};

#endif

#endif
