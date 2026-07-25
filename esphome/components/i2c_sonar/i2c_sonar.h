#pragma once

#include "esphome/core/component.h"
#include "esphome/components/i2c/i2c.h"
#include "esphome/components/sensor/sensor.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <freertos/timers.h>

#include <cstdint>

namespace esphome {
namespace i2c_sonar {

class I2CSonarSensor : public sensor::Sensor, public Component, public i2c::I2CDevice {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_update_interval(uint32_t interval) { this->update_interval_ms_ = interval; }
  void set_update_interval_ms(uint32_t interval) { this->update_interval_ms_ = interval; }
  void set_response_timeout_ms(uint32_t timeout) { this->response_timeout_ms_ = timeout; }
  void set_tank_height_cm(float tank_height) { this->tank_height_cm_ = tank_height; }
  void set_gallons_per_cm(float gallons_per_cm) { this->gallons_per_cm_ = gallons_per_cm; }

 protected:
  static void task_entry_(void *param);
  static void timer_callback_(TimerHandle_t timer);
  void task_loop_();
  void poll_once_();
  bool write_command_(uint8_t command);
  bool read_distance_um_(uint32_t *distance_um);
  void publish_from_task_(float gallons);

  uint32_t update_interval_ms_{10000};
  uint32_t response_timeout_ms_{100};
  float tank_height_cm_{183.0f};
  float gallons_per_cm_{5.6555f};

  TaskHandle_t task_handle_{nullptr};
  TimerHandle_t timer_handle_{nullptr};
  SemaphoreHandle_t state_mutex_{nullptr};
  uint8_t consecutive_failures_{0};
  bool pending_state_{false};
  float pending_gallons_{0.0f};
};

}  // namespace i2c_sonar
}  // namespace esphome
