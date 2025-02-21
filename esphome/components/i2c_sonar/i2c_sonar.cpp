#include "i2c_sonar.h"
#include "esphome/core/log.h"

namespace esphome {
namespace i2csonar {

#define DISTANCE_SDA 33
#define DISTANCE_SCL 27
#define I2C_ADDR 0x57

static const char* TAG = "i2csonar.sensor";

void setup() override {
    ESP_LOGCONFIG(TAG, "Setting up RCWL_1601 '%s'...", this->name_.c_str());
    ESP_LOGCONFIG(TAG, "Data pin is '%d'...", DISTANCE_SDA);
    ESP_LOGCONFIG(TAG, "Clock pin is '%d'...", DISTANCE_SCL);

    Wire1.begin(DISTANCE_SDA, DISTANCE_SCL);
    uint8_t result = 0;
    for(int i = 0; i < 2; i++) {
        int result = sonar.startScanning();
        if (result == 0) {
            ESP_LOGCONFIG(TAG, "Done setting up RCWL_1601 '%s'...", this->name_.c_str());
            break;
        }

        switch (result) {
        case 2:
        case 3:
            ESP_LOGE(TAG, "RCW-1601 responded with NACK");
            break;
        case 5:
            // didn't respond within timeout
            ESP_LOGE(TAG, "Unable to find the RCW-1601 on the i2c bus.");
            break;
        default:
            // includes 1 - data too long which shouldn't happen
            ESP_LOGE(TAG, "Unknown error: Can't communicate with RCW-1601");
            break;
        }
        delay(1000);
    }
}

void update() override {
    int32_t um = sonar.readUm();
    ESP_LOGD(TAG, "Sonar returned %ld um", um);
    if (um < 1) {
        ESP_LOGI(TAG, "Sensor returned spurious value < 1. Ignoring.");
        return;
    }

    float cmDepth = 183.0 - (float)um / 10000;
    int gallons = floor(cmDepth * 5.6555);
    publish_state(gallons);
}

} // namespace i2csonar
} // namespace esphome
