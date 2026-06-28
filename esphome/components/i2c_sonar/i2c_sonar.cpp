#include "i2c_sonar.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32
#include <cmath>
#include <cstring>

namespace esphome {
namespace i2c_sonar {

static const char *const TAG = "i2c_sonar.sensor";
static const uint32_t I2C_FREQUENCY = 50000;

void I2CSonarSensor::setup() {
  ESP_LOGCONFIG(TAG, "Running setup");
  this->state_mutex_ = xSemaphoreCreateMutex();
  if (this->state_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create state mutex");
    this->mark_failed();
    return;
  }

  BaseType_t created = xTaskCreatePinnedToCore(
      &I2CSonarSensor::task_entry_, "i2c_sonar", 4096, this, 1, &this->task_handle_, 1);
  if (created != pdPASS) {
    ESP_LOGE(TAG, "Failed to create sonar polling task");
    this->mark_failed();
  }
}

void I2CSonarSensor::loop() {
  if (this->state_mutex_ == nullptr)
    return;

  bool should_publish = false;
  float gallons = NAN;
  if (xSemaphoreTake(this->state_mutex_, 0) == pdTRUE) {
    should_publish = this->pending_state_;
    gallons = this->pending_gallons_;
    this->pending_state_ = false;
    xSemaphoreGive(this->state_mutex_);
  }

  if (should_publish)
    this->publish_state(gallons);
}

void I2CSonarSensor::dump_config() {
  LOG_SENSOR("", "I2C Sonar", this);
  ESP_LOGCONFIG(TAG,
                "  SDA Pin: GPIO%u\n"
                "  SCL Pin: GPIO%u\n"
                "  Address: 0x%02X\n"
                "  Update Interval: %" PRIu32 " ms\n"
                "  Measurement Delay: %" PRIu32 " ms\n"
                "  Response Timeout: %" PRIu32 " ms\n"
                "  Transaction Timeout: %" PRIu32 " ms\n"
                "  Tank Height: %.1f cm\n"
                "  Scale: %.4f gal/cm",
                this->sda_pin_, this->scl_pin_, this->address_, this->update_interval_ms_,
                this->measurement_delay_ms_, this->response_timeout_ms_, this->transaction_timeout_ms_,
                this->tank_height_cm_, this->gallons_per_cm_);
}

float I2CSonarSensor::get_setup_priority() const { return setup_priority::DATA; }

void I2CSonarSensor::task_entry_(void *param) {
  static_cast<I2CSonarSensor *>(param)->task_loop_();
}

void I2CSonarSensor::task_loop_() {
  uint8_t consecutive_failures = 0;

  for (;;) {
    if (!this->bus_ready_ && !this->setup_bus_()) {
      consecutive_failures++;
      vTaskDelay(pdMS_TO_TICKS(this->update_interval_ms_));
      continue;
    }

    uint32_t distance_um = 0;
    if (this->write_command_(1)) {
      vTaskDelay(pdMS_TO_TICKS(this->measurement_delay_ms_));
      if (this->read_distance_um_(&distance_um) && distance_um > 0) {
        const float depth_cm = this->tank_height_cm_ - (static_cast<float>(distance_um) / 10000.0f);
        const float gallons = floorf(depth_cm * this->gallons_per_cm_);
        ESP_LOGD(TAG, "Sonar returned %" PRIu32 " um, publishing %.0f gal", distance_um, gallons);
        this->publish_from_task_(gallons);
        consecutive_failures = 0;
      } else {
        ESP_LOGW(TAG, "Sonar read failed or returned an invalid distance");
        consecutive_failures++;
      }
    } else {
      ESP_LOGW(TAG, "Failed to start sonar measurement");
      consecutive_failures++;
    }

    if (consecutive_failures >= 3) {
      ESP_LOGW(TAG, "Resetting dedicated sonar I2C bus after repeated failures");
      this->reset_bus_();
      consecutive_failures = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(this->update_interval_ms_));
  }
}

bool I2CSonarSensor::setup_bus_() {
  i2c_config_t conf{};
  memset(&conf, 0, sizeof(conf));
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = static_cast<gpio_num_t>(this->sda_pin_);
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_io_num = static_cast<gpio_num_t>(this->scl_pin_);
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = I2C_FREQUENCY;

  esp_err_t err = i2c_param_config(this->port_, &conf);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
    return false;
  }

  err = i2c_driver_install(this->port_, I2C_MODE_MASTER, 0, 0, 0);
  if (err == ESP_ERR_INVALID_STATE) {
    i2c_driver_delete(this->port_);
    err = i2c_driver_install(this->port_, I2C_MODE_MASTER, 0, 0, 0);
  }
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
    return false;
  }

  this->bus_ready_ = true;
  return true;
}

void I2CSonarSensor::reset_bus_() {
  if (this->bus_ready_)
    i2c_driver_delete(this->port_);
  this->bus_ready_ = false;
}

bool I2CSonarSensor::write_command_(uint8_t command) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (this->address_ << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, command, true);
  i2c_master_stop(cmd);

  esp_err_t err = i2c_master_cmd_begin(this->port_, cmd, pdMS_TO_TICKS(this->transaction_timeout_ms_));
  i2c_cmd_link_delete(cmd);
  if (err != ESP_OK) {
    ESP_LOGV(TAG, "Write command failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

bool I2CSonarSensor::read_distance_um_(uint32_t *distance_um) {
  const TickType_t start = xTaskGetTickCount();
  const TickType_t deadline = start + pdMS_TO_TICKS(this->response_timeout_ms_);
  uint8_t bytes[3] = {0, 0, 0};

  while (xTaskGetTickCount() <= deadline) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (this->address_ << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, bytes, sizeof(bytes), I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(this->port_, cmd, pdMS_TO_TICKS(this->transaction_timeout_ms_));
    i2c_cmd_link_delete(cmd);
    if (err == ESP_OK) {
      *distance_um = (static_cast<uint32_t>(bytes[0]) << 16) | (static_cast<uint32_t>(bytes[1]) << 8) |
                     static_cast<uint32_t>(bytes[2]);
      return true;
    }

    ESP_LOGV(TAG, "Read attempt failed: %s", esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  return false;
}

void I2CSonarSensor::publish_from_task_(float gallons) {
  if (this->state_mutex_ == nullptr)
    return;

  if (xSemaphoreTake(this->state_mutex_, pdMS_TO_TICKS(20)) == pdTRUE) {
    this->pending_gallons_ = gallons;
    this->pending_state_ = true;
    xSemaphoreGive(this->state_mutex_);
  }
}

}  // namespace i2c_sonar
}  // namespace esphome

#endif  // USE_ESP32
