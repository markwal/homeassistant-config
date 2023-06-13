#include "esphome.h"

#include "RCWL_1601_i2c.h"
#define DISTANCE_SDA 33
#define DISTANCE_SCL 27
#define I2C_ADDR 0x57

class I2cSonarSensor : public PollingComponent, public Sensor {
    public:
        I2C_Sonar sonar = I2C_Sonar(I2C_ADDR, &Wire1);

        I2cSonarSensor() : PollingComponent(15000) {}

        void setup() override {
            Wire1.begin(DISTANCE_SDA, DISTANCE_SCL);
            uint8_t result = 0;
            for(int i = 0; i < 2; i++) {
                int result = sonar.startScanning();
                if (result == 0)
                    break;

                switch (result) {
                case 2:
                case 3:
                    ESP_LOGE("sonar", "RCW-1601 responded with NACK");
                    break;
                case 5:
                    // didn't respond within timeout
                    ESP_LOGE("sonar", "Unable to find the RCW-1601 on the i2c bus.");
                    break;
                default:
                    // includes 1 - data too long which shouldn't happen
                    ESP_LOGE("sonar", "Unknown error: Can't communicate with RCW-1601");
                    break;
                }
                delay(1000);
            }
        }

        void update() override {
            int32_t um = sonar.readUm(500);
            ESP_LOGD("sonar", "Sonar returned %ld um", um);
            if (um < 1) {
                ESP_LOGI("sonar", "Sensor returned spurious value < 1. Ignoring.");
                return;
            }

            float cmDepth = 183.0 - (float)um / 10000;
            int gallons = floor(cmDepth * 5.6555);
            publish_state(gallons);
        }
};
