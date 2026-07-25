#include "i2c_sonar.h"
#include "esphome/core/log.h"

#include <cmath>

namespace esphome {
namespace i2c_sonar {

static const char *const TAG = "i2c_sonar.sensor";
static const uint8_t START_SCANNING_COMMAND = 1;

void I2CSonarSensor::setup() {
  ESP_LOGCONFIG(TAG, "Running setup");
  this->state_mutex_ = xSemaphoreCreateMutex();
  if (this->state_mutex_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create state mutex");
    this->mark_failed();
    return;
  }

  const TickType_t timer_period = pdMS_TO_TICKS(this->update_interval_ms_);
  this->timer_handle_ =
      xTimerCreate("i2c_sonar", timer_period > 0 ? timer_period : 1, pdTRUE, this, &I2CSonarSensor::timer_callback_);
  if (this->timer_handle_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create sonar polling timer");
    this->mark_failed();
    return;
  }

  if (!this->write_command_(START_SCANNING_COMMAND))
    ESP_LOGW(TAG, "Failed to prime sonar scanning; will retry on the next poll");

  BaseType_t created = xTaskCreatePinnedToCore(
      &I2CSonarSensor::task_entry_, "i2c_sonar", 4096, this, 1, &this->task_handle_, 1);
  if (created != pdPASS) {
    ESP_LOGE(TAG, "Failed to create sonar polling task");
    xTimerDelete(this->timer_handle_, 0);
    this->timer_handle_ = nullptr;
    this->mark_failed();
    return;
  }

  if (xTimerStart(this->timer_handle_, 0) != pdPASS) {
    ESP_LOGE(TAG, "Failed to start sonar polling timer");
    vTaskDelete(this->task_handle_);
    this->task_handle_ = nullptr;
    xTimerDelete(this->timer_handle_, 0);
    this->timer_handle_ = nullptr;
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
  LOG_I2C_DEVICE(this);
  ESP_LOGCONFIG(TAG,
                "  Update Interval: %" PRIu32 " ms\n"
                "  Response Timeout: %" PRIu32 " ms\n"
                "  Tank Height: %.1f cm\n"
                "  Scale: %.4f gal/cm",
                this->update_interval_ms_, this->response_timeout_ms_, this->tank_height_cm_,
                this->gallons_per_cm_);
}

float I2CSonarSensor::get_setup_priority() const { return setup_priority::DATA; }

void I2CSonarSensor::task_entry_(void *param) {
  static_cast<I2CSonarSensor *>(param)->task_loop_();
}

void I2CSonarSensor::timer_callback_(TimerHandle_t timer) {
  auto *sensor = static_cast<I2CSonarSensor *>(pvTimerGetTimerID(timer));
  if (sensor != nullptr && sensor->task_handle_ != nullptr)
    xTaskNotifyGive(sensor->task_handle_);
}

void I2CSonarSensor::task_loop_() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    this->poll_once_();
  }
}

void I2CSonarSensor::poll_once_() {
  uint32_t distance_um = 0;
  bool success = false;

  if (this->read_distance_um_(&distance_um) && distance_um > 0) {
    float depth_cm = this->tank_height_cm_ - (static_cast<float>(distance_um) / 10000.0f);
    if (depth_cm < 0.0f)
      depth_cm = 0.0f;
    const float gallons = floorf(depth_cm * this->gallons_per_cm_);
    ESP_LOGD(TAG, "Sonar returned %" PRIu32 " um, publishing %.0f gal", distance_um, gallons);
    this->publish_from_task_(gallons);
    success = true;
  } else {
    ESP_LOGW(TAG, "Sonar read failed or returned an invalid distance");
  }

  if (!this->write_command_(START_SCANNING_COMMAND)) {
    ESP_LOGW(TAG, "Failed to prime sonar scanning");
    success = false;
  }

  if (success) {
    this->consecutive_failures_ = 0;
  } else {
    if (this->consecutive_failures_ < UINT8_MAX)
      this->consecutive_failures_++;
    if (this->consecutive_failures_ == 3)
      ESP_LOGW(TAG, "Sonar has failed %u consecutive polls", this->consecutive_failures_);
  }
}

bool I2CSonarSensor::write_command_(uint8_t command) {
  return this->write(&command, 1) == i2c::ERROR_OK;
}

bool I2CSonarSensor::read_distance_um_(uint32_t *distance_um) {
  uint8_t bytes[3] = {0, 0, 0};
  const TickType_t start = xTaskGetTickCount();
  const TickType_t deadline = start + pdMS_TO_TICKS(this->response_timeout_ms_);

  do {
    if (this->read(bytes, sizeof(bytes)) == i2c::ERROR_OK) {
      *distance_um = (static_cast<uint32_t>(bytes[0]) << 16) | (static_cast<uint32_t>(bytes[1]) << 8) |
                     static_cast<uint32_t>(bytes[2]);
      return true;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  } while (xTaskGetTickCount() <= deadline);

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
