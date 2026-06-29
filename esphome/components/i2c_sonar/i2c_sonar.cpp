#include "i2c_sonar.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32
#include <cmath>
#include "esphome/core/hal.h"

namespace esphome {
namespace i2c_sonar {

static const char *const TAG = "i2c_sonar.sensor";
static const uint32_t BIT_DELAY_US = 10;

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
  vTaskDelay(pdMS_TO_TICKS(5000));

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
  this->recover_bus_();
  this->bus_ready_ = true;
  return true;
}

void I2CSonarSensor::reset_bus_() {
  this->bus_ready_ = false;
  this->recover_bus_();
}

void I2CSonarSensor::recover_bus_() {
  const gpio_num_t sda_pin = static_cast<gpio_num_t>(this->sda_pin_);
  const gpio_num_t scl_pin = static_cast<gpio_num_t>(this->scl_pin_);

  gpio_set_level(sda_pin, 1);
  gpio_set_level(scl_pin, 1);

  gpio_config_t scl_config{};
  scl_config.pin_bit_mask = 1ULL << this->scl_pin_;
  scl_config.mode = GPIO_MODE_INPUT_OUTPUT_OD;
  scl_config.pull_up_en = GPIO_PULLUP_ENABLE;
  scl_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  scl_config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&scl_config);

  gpio_config_t sda_config{};
  sda_config.pin_bit_mask = 1ULL << this->sda_pin_;
  sda_config.mode = GPIO_MODE_INPUT_OUTPUT_OD;
  sda_config.pull_up_en = GPIO_PULLUP_ENABLE;
  sda_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  sda_config.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&sda_config);

  delayMicroseconds(10);
  ESP_LOGD(TAG, "Recovering sonar bus: SDA=%d SCL=%d", gpio_get_level(sda_pin), gpio_get_level(scl_pin));

  for (uint8_t i = 0; i < 9; i++) {
    this->set_scl_(false);
    delayMicroseconds(10);
    this->set_scl_(true);
    delayMicroseconds(10);
  }

  this->set_sda_(false);
  delayMicroseconds(10);
  this->set_scl_(true);
  delayMicroseconds(10);
  this->set_sda_(true);
  delayMicroseconds(10);

  ESP_LOGD(TAG, "Recovered sonar bus: SDA=%d SCL=%d", gpio_get_level(sda_pin), gpio_get_level(scl_pin));
}

void I2CSonarSensor::set_sda_(bool high) {
  gpio_set_level(static_cast<gpio_num_t>(this->sda_pin_), high ? 1 : 0);
}

void I2CSonarSensor::set_scl_(bool high) {
  gpio_set_level(static_cast<gpio_num_t>(this->scl_pin_), high ? 1 : 0);
}

bool I2CSonarSensor::read_sda_() {
  return gpio_get_level(static_cast<gpio_num_t>(this->sda_pin_)) != 0;
}

bool I2CSonarSensor::read_scl_() {
  return gpio_get_level(static_cast<gpio_num_t>(this->scl_pin_)) != 0;
}

bool I2CSonarSensor::wait_scl_high_(uint32_t timeout_us) {
  uint32_t waited = 0;
  while (!this->read_scl_()) {
    if (waited >= timeout_us)
      return false;
    delayMicroseconds(10);
    waited += 10;
  }
  return true;
}

bool I2CSonarSensor::start_condition_() {
  this->set_sda_(true);
  this->set_scl_(true);
  if (!this->wait_scl_high_(this->transaction_timeout_ms_ * 1000)) {
    ESP_LOGV(TAG, "Start failed: SCL held low");
    return false;
  }
  delayMicroseconds(BIT_DELAY_US);
  if (!this->read_sda_()) {
    ESP_LOGV(TAG, "Start failed: SDA held low");
    return false;
  }
  this->set_sda_(false);
  delayMicroseconds(BIT_DELAY_US);
  this->set_scl_(false);
  delayMicroseconds(BIT_DELAY_US);
  return true;
}

bool I2CSonarSensor::stop_condition_() {
  this->set_sda_(false);
  delayMicroseconds(BIT_DELAY_US);
  this->set_scl_(true);
  if (!this->wait_scl_high_(this->transaction_timeout_ms_ * 1000)) {
    ESP_LOGV(TAG, "Stop failed: SCL held low");
    return false;
  }
  delayMicroseconds(BIT_DELAY_US);
  this->set_sda_(true);
  delayMicroseconds(BIT_DELAY_US);
  return true;
}

bool I2CSonarSensor::write_byte_(uint8_t byte) {
  for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
    this->set_sda_((byte & mask) != 0);
    delayMicroseconds(BIT_DELAY_US);
    this->set_scl_(true);
    if (!this->wait_scl_high_(this->transaction_timeout_ms_ * 1000)) {
      ESP_LOGV(TAG, "Write failed: SCL held low");
      return false;
    }
    delayMicroseconds(BIT_DELAY_US);
    this->set_scl_(false);
    delayMicroseconds(BIT_DELAY_US);
  }

  this->set_sda_(true);
  delayMicroseconds(BIT_DELAY_US);
  this->set_scl_(true);
  if (!this->wait_scl_high_(this->transaction_timeout_ms_ * 1000)) {
    ESP_LOGV(TAG, "ACK failed: SCL held low");
    return false;
  }
  delayMicroseconds(BIT_DELAY_US);
  bool ack = !this->read_sda_();
  this->set_scl_(false);
  delayMicroseconds(BIT_DELAY_US);

  if (!ack)
    ESP_LOGV(TAG, "Write byte 0x%02X not acknowledged", byte);
  return ack;
}

bool I2CSonarSensor::read_byte_(uint8_t *byte, bool ack) {
  uint8_t value = 0;
  this->set_sda_(true);

  for (uint8_t i = 0; i < 8; i++) {
    value <<= 1;
    this->set_scl_(true);
    if (!this->wait_scl_high_(this->transaction_timeout_ms_ * 1000)) {
      ESP_LOGV(TAG, "Read failed: SCL held low");
      return false;
    }
    delayMicroseconds(BIT_DELAY_US);
    if (this->read_sda_())
      value |= 1;
    this->set_scl_(false);
    delayMicroseconds(BIT_DELAY_US);
  }

  this->set_sda_(!ack);
  delayMicroseconds(BIT_DELAY_US);
  this->set_scl_(true);
  if (!this->wait_scl_high_(this->transaction_timeout_ms_ * 1000))
    return false;
  delayMicroseconds(BIT_DELAY_US);
  this->set_scl_(false);
  this->set_sda_(true);
  delayMicroseconds(BIT_DELAY_US);

  *byte = value;
  return true;
}

bool I2CSonarSensor::write_command_(uint8_t command) {
  bool ok = this->start_condition_() && this->write_byte_((this->address_ << 1) | 0) && this->write_byte_(command);
  this->stop_condition_();
  return ok;
}

bool I2CSonarSensor::read_distance_um_(uint32_t *distance_um) {
  uint8_t bytes[3] = {0, 0, 0};

  const TickType_t start = xTaskGetTickCount();
  const TickType_t deadline = start + pdMS_TO_TICKS(this->response_timeout_ms_);

  while (xTaskGetTickCount() <= deadline) {
    if (this->start_condition_() && this->write_byte_((this->address_ << 1) | 1) &&
        this->read_byte_(&bytes[0], true) && this->read_byte_(&bytes[1], true) &&
        this->read_byte_(&bytes[2], false)) {
      this->stop_condition_();
      *distance_um = (static_cast<uint32_t>(bytes[0]) << 16) | (static_cast<uint32_t>(bytes[1]) << 8) |
                     static_cast<uint32_t>(bytes[2]);
      return true;
    }

    this->stop_condition_();
    ESP_LOGV(TAG, "Read attempt failed");
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
