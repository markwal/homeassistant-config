#pragma once

#include "RCWL_1601_i2c.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"

namespace esphome {
namespace i2csonar {

#define DISTANCE_SDA 33
#define DISTANCE_SCL 27
#define I2C_ADDR 0x57

class I2cSonarSensor : public PollingComponent, public sensonr::Sensor {
    public:
        I2C_Sonar sonar = I2C_Sonar(I2C_ADDR, &Wire1);

        I2cSonarSensor() : PollingComponent(15000) {}

        void setup() override;

        void update() override;
};

} // namespace i2csonar
} // namespace esphome
