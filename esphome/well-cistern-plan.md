# Well Cistern Sonar Rework Plan

## Summary

- Keep the existing RCWL onboard I2C hardware path on GPIO33/GPIO27.
- Switch `esphome/well-cistern.yaml` from Arduino to ESP-IDF while preserving the current 15 second sonar cadence and tank conversion math.
- Replace the stale include/custom-sensor approach with a real local ESPHome external component that owns its own ESP-IDF task.

## Key Changes

- Update `esphome/well-cistern.yaml`:
  - Change `esp32.framework.type` to `esp-idf`.
  - Remove `esphome.includes` and Arduino-only `libraries` entries for `Wire` and `RCWL_1601_i2c`.
  - Keep the main ESPHome I2C bus on GPIO22/GPIO20 for BME680 and SH1107.
  - Replace the depth sensor with `platform: i2c_sonar`, `id: depth`, `name: Depth`, `device_class: volume`, `unit_of_measurement: gal`, using GPIO33/GPIO27, address `0x57`, and a 15 second polling interval.
- Add a local external component under `esphome/components/i2c_sonar/`:
  - `sensor.py` defines the YAML schema and codegen for pins, address, interval, tank height `183.0 cm`, and conversion `5.6555 gal/cm`.
  - C++ component extends `sensor::Sensor` and `Component`, not `PollingComponent`.
  - `setup()` starts a FreeRTOS task via `xTaskCreatePinnedToCore` or `xTaskCreate`; the task performs all sonar I2C setup, ping command, wait/read, conversion, and retry timing.
  - Use ESP-IDF I2C APIs directly on the dedicated sonar bus, separate from ESPHome's `bus_a`, so a bad sonar transaction cannot stall the shared display/BME bus.
  - Do not call `publish_state()` directly from the worker task; store the latest reading/status in thread-safe fields and publish from `loop()` on ESPHome's normal context.
- Preserve existing display behavior:
  - Keep the `depth_page` reading `id(depth).state`.
  - Leave button/display automation logic unchanged unless compilation requires only small ESP-IDF compatibility fixes.

## Failure Handling

- On each cycle, write command byte `1` to address `0x57`, wait for the measurement window, then read 3 bytes.
- Treat NACK, timeout, short read, and values `< 1` as failed samples: log a warning and keep the previous Home Assistant state.
- Add bounded timeouts so the worker task never waits forever, even if the RCWL controller holds or misbehaves on the bus.
- Optionally attempt bus cleanup/reinitialization after repeated failures, but do not reboot or mark the whole ESPHome node failed from sonar failures alone.

## Test Plan

- Run ESPHome config validation/build for `esphome/well-cistern.yaml`.
- If the local `esphome` command still fails with missing `argcomplete`, use the dedicated `.venv-esphome` environment instead.
- Confirm generated code uses ESP-IDF I2C for the main bus and compiles the local `i2c_sonar` component.
- After flashing, verify logs show the sonar worker starts, polls about every 15 seconds, and publishes `Depth` in gallons without blocking display updates, Wi-Fi, API, or BME680 readings.

## Assumptions

- Hardware remains wired through the RCWL onboard I2C controller.
- Sonar pins remain GPIO33 SDA and GPIO27 SCL.
- Main I2C pins remain GPIO22 SDA and GPIO20 SCL.
- Tank conversion remains `gallons = floor((183.0 - micrometers / 10000.0) * 5.6555)`.
