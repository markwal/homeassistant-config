#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"

#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <freertos/timers.h>

#include <cstdint>

namespace esphome {
namespace i2c_sonar {

class I2CSonarSensor : public sensor::Sensor, public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  void set_sda_pin(uint8_t pin) { this->sda_pin_ = pin; }
  void set_scl_pin(uint8_t pin) { this->scl_pin_ = pin; }
  void set_address(uint8_t address) { this->address_ = address; }
  void set_update_interval(uint32_t interval) { this->update_interval_ms_ = interval; }
  void set_update_interval_ms(uint32_t interval) { this->update_interval_ms_ = interval; }
  void set_measurement_delay_ms(uint32_t delay) { this->measurement_delay_ms_ = delay; }
  void set_response_timeout_ms(uint32_t timeout) { this->response_timeout_ms_ = timeout; }
  void set_transaction_timeout_ms(uint32_t timeout) { this->transaction_timeout_ms_ = timeout; }
  void set_tank_height_cm(float tank_height) { this->tank_height_cm_ = tank_height; }
  void set_gallons_per_cm(float gallons_per_cm) { this->gallons_per_cm_ = gallons_per_cm; }

 protected:
  static void task_entry_(void *param);
  static void timer_callback_(TimerHandle_t timer);
  void task_loop_();
  void poll_once_();
  bool setup_bus_();
  void reset_bus_();
  void recover_bus_();
  void set_sda_(bool high);
  void set_scl_(bool high);
  bool read_sda_();
  bool read_scl_();
  bool wait_scl_high_(uint32_t timeout_us);
  bool start_condition_();
  bool stop_condition_();
  bool write_byte_(uint8_t byte);
  bool read_byte_(uint8_t *byte, bool ack);
  bool write_command_(uint8_t command);
  bool read_distance_um_(uint32_t *distance_um);
  void publish_from_task_(float gallons);

  uint8_t sda_pin_{33};
  uint8_t scl_pin_{27};
  uint8_t address_{0x57};
  uint32_t update_interval_ms_{10000};
  uint32_t measurement_delay_ms_{500};
  uint32_t response_timeout_ms_{100};
  uint32_t transaction_timeout_ms_{50};
  float tank_height_cm_{183.0f};
  float gallons_per_cm_{5.6555f};

  TaskHandle_t task_handle_{nullptr};
  TimerHandle_t timer_handle_{nullptr};
  SemaphoreHandle_t state_mutex_{nullptr};
  uint8_t consecutive_failures_{0};
  bool bus_ready_{false};
  bool pending_state_{false};
  float pending_gallons_{0.0f};
};

}  // namespace i2c_sonar
}  // namespace esphome
