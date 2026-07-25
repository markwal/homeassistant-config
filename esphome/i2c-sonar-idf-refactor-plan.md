# Refactor i2c_sonar Back To ESPHome/ESP-IDF I2C

## Summary

Replace the custom bit-banged I2C implementation with ESPHome's normal `i2c::I2CDevice` path, using ESPHome's ESP-IDF-backed I2C bus management.

Keep the sonar isolated on its own hardware I2C bus, but define that bus in YAML instead of inside the component.

Mirror the working Arduino library sequence: prime scanning with command `1`, then each poll reads 3 bytes and immediately writes command `1` again for the next poll.

Do not use empty address probes for readiness; this device NACKs those even when real transactions work.

## Key Changes

- Add a second ESPHome I2C bus for sonar on GPIO33/GPIO27 with `scan: false`.
- Make `i2c_sonar` inherit from `i2c::I2CDevice` and use `i2c_id: sonar_bus`, `address: 0x57`.
- Remove bit-banged GPIO I2C, manual ACK/start/stop handling, and custom bus recovery.
- Keep the `xTimerCreate` + worker task structure.
- Prime scanning once on startup with byte `1`; each poll reads 3 bytes, publishes gallons, then writes byte `1` again.
- Update schema to use `i2c.i2c_device_schema(0x57)` and remove custom SDA/SCL/transaction-timeout options.
- Keep ESP32 + ESP-IDF validation requirements.

## Test Plan

- Run `.venv-esphome\Scripts\esphome.exe config esphome\well-cistern.yaml`.
- Run `.venv-esphome\Scripts\esphome.exe compile esphome\well-cistern.yaml`.
- After flashing, verify sonar bus does not scan, `Depth` publishes about every 10 seconds, and empty-probe NACKs disappear.

## Assumptions

- The working Arduino sketch is the source of truth.
- The commented-out `0x1` transaction is not required.
- Empty `beginTransmission/endTransmission` NACKs are expected and should not be treated as sensor absence.
- The sonar should stay on a dedicated I2C bus.
