import esphome.codegen as cg
from esphome.components import i2c, sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_TIMEOUT,
    CONF_UPDATE_INTERVAL,
    DEVICE_CLASS_DISTANCE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CENTIMETER,
)

i2c_sonar_ns = cg.esphome_ns.namespace("i2c_sonar")
I2CSonarSensor = i2c_sonar_ns.class_(
    "I2CSonarSensor", sensor.Sensor, cg.Component, i2c.I2CDevice
)


CONFIG_SCHEMA = cv.All(
    sensor.sensor_schema(
        I2CSonarSensor,
        unit_of_measurement=UNIT_CENTIMETER,
        accuracy_decimals=1,
        device_class=DEVICE_CLASS_DISTANCE,
        state_class=STATE_CLASS_MEASUREMENT,
    )
    .extend(
        {
            cv.Optional(CONF_UPDATE_INTERVAL, default="10s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_TIMEOUT, default="100ms"): cv.positive_time_period_milliseconds,
        }
    )
    .extend(i2c.i2c_device_schema(0x57))
    .extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)

    cg.add(var.set_update_interval_ms(config[CONF_UPDATE_INTERVAL].total_milliseconds))
    cg.add(var.set_response_timeout_ms(config[CONF_TIMEOUT].total_milliseconds))
