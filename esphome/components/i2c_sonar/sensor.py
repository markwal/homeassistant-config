from esphome import pins
import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ADDRESS,
    CONF_TIMEOUT,
    CONF_UPDATE_INTERVAL,
    DEVICE_CLASS_VOLUME,
    STATE_CLASS_MEASUREMENT,
)

CONF_GALLONS_PER_CM = "gallons_per_cm"
CONF_MEASUREMENT_DELAY = "measurement_delay"
CONF_SCL_PIN = "scl_pin"
CONF_SDA_PIN = "sda_pin"
CONF_TANK_HEIGHT = "tank_height"
CONF_TRANSACTION_TIMEOUT = "transaction_timeout"

i2c_sonar_ns = cg.esphome_ns.namespace("i2c_sonar")
I2CSonarSensor = i2c_sonar_ns.class_(
    "I2CSonarSensor", sensor.Sensor, cg.Component
)

CONFIG_SCHEMA = sensor.sensor_schema(
    I2CSonarSensor,
    unit_of_measurement="gal",
    accuracy_decimals=0,
    device_class=DEVICE_CLASS_VOLUME,
    state_class=STATE_CLASS_MEASUREMENT,
).extend(
    {
        cv.Required(CONF_SDA_PIN): pins.internal_gpio_pin_number,
        cv.Required(CONF_SCL_PIN): pins.internal_gpio_pin_number,
        cv.Optional(CONF_ADDRESS, default=0x57): cv.i2c_address,
        cv.Optional(CONF_UPDATE_INTERVAL, default="15s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_MEASUREMENT_DELAY, default="500ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_TIMEOUT, default="100ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_TRANSACTION_TIMEOUT, default="50ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_TANK_HEIGHT, default=183.0): cv.positive_float,
        cv.Optional(CONF_GALLONS_PER_CM, default=5.6555): cv.positive_float,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)

    cg.add(var.set_sda_pin(config[CONF_SDA_PIN]))
    cg.add(var.set_scl_pin(config[CONF_SCL_PIN]))
    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(var.set_update_interval_ms(config[CONF_UPDATE_INTERVAL].total_milliseconds))
    cg.add(var.set_measurement_delay_ms(config[CONF_MEASUREMENT_DELAY].total_milliseconds))
    cg.add(var.set_response_timeout_ms(config[CONF_TIMEOUT].total_milliseconds))
    cg.add(var.set_transaction_timeout_ms(config[CONF_TRANSACTION_TIMEOUT].total_milliseconds))
    cg.add(var.set_tank_height_cm(config[CONF_TANK_HEIGHT]))
    cg.add(var.set_gallons_per_cm(config[CONF_GALLONS_PER_CM]))
