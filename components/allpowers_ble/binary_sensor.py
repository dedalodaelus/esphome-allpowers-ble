# SPDX-License-Identifier: MIT
#
# ESPHome BLE component for compatible ALLPOWERS power stations.
# BLE protocol mapping and command logic are derived from:
# https://github.com/madninjaskillz/allpowers-ble

import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_BATTERY_CHARGING,
    DEVICE_CLASS_CONNECTIVITY,
    DEVICE_CLASS_PROBLEM,
    ENTITY_CATEGORY_DIAGNOSTIC,
)

from . import CONF_ALLPOWERS_BLE_ID, AllpowersBLE

CONF_CONNECTED = "connected"
CONF_DATA_VALID = "data_valid"
CONF_CONTROLS_AVAILABLE = "controls_available"
CONF_SETTINGS_AVAILABLE = "settings_available"
CONF_AC_OUTPUT = "ac_output"
CONF_DC_OUTPUT = "dc_output"
CONF_ECO_MODE = "eco_mode"
CONF_LIGHT = "light"
CONF_CHARGING = "charging"
CONF_DISCHARGING = "discharging"
CONF_PROTOCOL_ERROR = "protocol_error"

# Connection, telemetry readiness and settings readiness are deliberately
# distinct. Output writes require a fresh status bitmap; ECO writes require a
# fresh settings bitmap because the two command families preserve different data.
BINARY_SENSORS = {
    CONF_CONNECTED: (
        "set_connected_binary_sensor",
        binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    ),
    CONF_DATA_VALID: (
        "set_data_valid_binary_sensor",
        binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    ),
    CONF_CONTROLS_AVAILABLE: (
        "set_controls_available_binary_sensor",
        binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    ),
    CONF_SETTINGS_AVAILABLE: (
        "set_settings_available_binary_sensor",
        binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_CONNECTIVITY,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    ),
    CONF_AC_OUTPUT: (
        "set_ac_output_binary_sensor",
        binary_sensor.binary_sensor_schema(),
    ),
    CONF_DC_OUTPUT: (
        "set_dc_output_binary_sensor",
        binary_sensor.binary_sensor_schema(),
    ),
    CONF_ECO_MODE: (
        "set_eco_mode_binary_sensor",
        binary_sensor.binary_sensor_schema(),
    ),
    CONF_LIGHT: (
        "set_light_binary_sensor",
        binary_sensor.binary_sensor_schema(),
    ),
    CONF_CHARGING: (
        "set_charging_binary_sensor",
        binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_BATTERY_CHARGING),
    ),
    CONF_DISCHARGING: (
        "set_discharging_binary_sensor",
        binary_sensor.binary_sensor_schema(),
    ),
    CONF_PROTOCOL_ERROR: (
        "set_protocol_error_binary_sensor",
        binary_sensor.binary_sensor_schema(
            device_class=DEVICE_CLASS_PROBLEM,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    ),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ALLPOWERS_BLE_ID): cv.use_id(AllpowersBLE),
        **{cv.Optional(key): schema for key, (_, schema) in BINARY_SENSORS.items()},
    }
)


async def to_code(config):
    """Attach configured binary sensors to the shared BLE component."""

    parent = await cg.get_variable(config[CONF_ALLPOWERS_BLE_ID])
    for key, (setter, _) in BINARY_SENSORS.items():
        if conf := config.get(key):
            sens = await binary_sensor.new_binary_sensor(conf)
            cg.add(getattr(parent, setter)(sens))
