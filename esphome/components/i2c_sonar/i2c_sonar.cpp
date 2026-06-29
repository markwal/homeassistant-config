#include "i2c_sonar.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32
#include <cmath>

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
  i2c_master_bus_config_t bus_config{};
  bus_config.i2c_port = this->port_;
  bus_config.sda_io_num = static_cast<gpio_num_t>(this->sda_pin_);
  bus_config.scl_io_num = static_cast<gpio_num_t>(this->scl_pin_);
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;

  esp_err_t err = i2c_new_master_bus(&bus_config, &this->bus_handle_);
  if (err == ESP_ERR_INVALID_STATE) {
    this->reset_bus_();
    err = i2c_new_master_bus(&bus_config, &this->bus_handle_);
  }
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
    return false;
  }

  i2c_device_config_t device_config{};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = this->address_;
  device_config.scl_speed_hz = I2C_FREQUENCY;

  err = i2c_master_bus_add_device(this->bus_handle_, &device_config, &this->device_handle_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
    this->reset_bus_();
    return false;
  }

  this->bus_ready_ = true;
  return true;
}

void I2CSonarSensor::reset_bus_() {
  if (this->device_handle_ != nullptr) {
    i2c_master_bus_rm_device(this->device_handle_);
    this->device_handle_ = nullptr;
  }
  if (this->bus_handle_ != nullptr) {
    i2c_del_master_bus(this->bus_handle_);
    this->bus_handle_ = nullptr;
  }
  this->bus_ready_ = false;
}

bool I2CSonarSensor::write_command_(uint8_t command) {
  if (this->device_handle_ == nullptr)
    return false;

  esp_err_t err = i2c_master_transmit(this->device_handle_, &command, 1, this->transaction_timeout_ms_);
  if (err != ESP_OK) {
    ESP_LOGV(TAG, "Write command failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

bool I2CSonarSensor::read_distance_um_(uint32_t *distance_um) {
  if (this->device_handle_ == nullptr)
    return false;

  const TickType_t start = xTaskGetTickCount();
  const TickType_t deadline = start + pdMS_TO_TICKS(this->response_timeout_ms_);
  uint8_t bytes[3] = {0, 0, 0};

  while (xTaskGetTickCount() <= deadline) {
    esp_err_t err = i2c_master_receive(this->device_handle_, bytes, sizeof(bytes), this->transaction_timeout_ms_);
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
