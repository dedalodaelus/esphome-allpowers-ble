# SPDX-License-Identifier: MIT
#
# ESPHome BLE component for compatible ALLPOWERS power stations.
# BLE protocol mapping and command logic are derived from:
# https://github.com/madninjaskillz/allpowers-ble

import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_ALLPOWERS_BLE_ID, AllpowersBLE, AllpowersBLEStationNameTextSensor

CONF_HARDWARE_VERSION = "hardware_version"
CONF_FIRMWARE_VERSION = "firmware_version"
CONF_STATION_NAME = "station_name"
CONF_LAST_PROTOCOL_ERROR = "last_protocol_error"

# These versions describe the power station and arrive in the settings report;
# they are unrelated to the ESPHome version running on the BLE gateway.
TEXT_SENSORS = {
    CONF_HARDWARE_VERSION: (
        "set_hardware_version_text_sensor",
        text_sensor.text_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    ),
    CONF_FIRMWARE_VERSION: (
        "set_firmware_version_text_sensor",
        text_sensor.text_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    ),
    CONF_LAST_PROTOCOL_ERROR: (
        "set_last_protocol_error_text_sensor",
        text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:alert-circle-outline",
        ),
    ),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ALLPOWERS_BLE_ID): cv.use_id(AllpowersBLE),
        cv.Optional(CONF_STATION_NAME): text_sensor.text_sensor_schema(
            AllpowersBLEStationNameTextSensor,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:bluetooth",
        ),
        **{cv.Optional(key): schema for key, (_, schema) in TEXT_SENSORS.items()},
    }
)


async def to_code(config):
    """Attach the persistent station name and version diagnostics."""

    parent = await cg.get_variable(config[CONF_ALLPOWERS_BLE_ID])
    if conf := config.get(CONF_STATION_NAME):
        sens = await text_sensor.new_text_sensor(conf)
        cg.add(sens.set_parent(parent))
        cg.add(parent.set_station_name_text_sensor(sens))
        await cg.register_component(sens, conf)

    for key, (setter, _) in TEXT_SENSORS.items():
        if conf := config.get(key):
            sens = await text_sensor.new_text_sensor(conf)
            cg.add(getattr(parent, setter)(sens))
