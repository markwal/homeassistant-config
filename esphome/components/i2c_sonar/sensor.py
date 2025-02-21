import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    STATE_CLASS_MEASUREMENT,
    ICON_WATER,
)

DEPENDENCIES = ["esp32"]

i2csonar_ns = cg.esphome_ns.namespace("i2csonar")
I2cSonar = i2csonar_ns.class_("I2cSonarSensor", sensor.Sensor, cg.PollingComponent)

CONFIG_SCHEMA = sensor.sensor_schema(
    I2cSonar,
    unit_of_measurement="gal",
    icon=ICON_WATER,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_VOLUME,
    state_class=STATE_CLASS_MEASUREMENT,
).extend(cv.polling_component_schema("15s"))


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)
