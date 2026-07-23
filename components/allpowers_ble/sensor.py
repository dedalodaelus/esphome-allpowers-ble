# SPDX-License-Identifier: MIT
#
# ESPHome BLE component for compatible ALLPOWERS power stations.
# BLE protocol mapping and command logic are derived from:
# https://github.com/madninjaskillz/allpowers-ble

import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_BATTERY,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_POWER,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_HERTZ,
    UNIT_MINUTE,
    UNIT_PERCENT,
    UNIT_SECOND,
    UNIT_WATT,
)

from . import CONF_ALLPOWERS_BLE_ID, AllpowersBLE

CONF_SOC = "state_of_charge"
CONF_INPUT_POWER = "input_power"
CONF_OUTPUT_POWER = "output_power"
CONF_REMAINING_TIME = "remaining_time"
CONF_AC_FREQUENCY = "ac_frequency"
CONF_STATUS_BYTE = "status_byte"
CONF_PACKET_LENGTH = "packet_length"
CONF_PROTOCOL_ERROR_COUNT = "protocol_error_count"
CONF_CONSECUTIVE_PROTOCOL_ERRORS = "consecutive_protocol_errors"
CONF_LAST_PROTOCOL_ERROR_UPTIME = "last_protocol_error_uptime"

# Each entry pairs an ESPHome schema with the C++ setter receiving the created
# entity. Keeping both in one table prevents schema/codegen drift as optional
# telemetry fields are added.
SENSORS = {
    CONF_SOC: (
        "set_soc_sensor",
        sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_BATTERY,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    ),
    CONF_INPUT_POWER: (
        "set_input_power_sensor",
        sensor.sensor_schema(
            unit_of_measurement=UNIT_WATT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    ),
    CONF_OUTPUT_POWER: (
        "set_output_power_sensor",
        sensor.sensor_schema(
            unit_of_measurement=UNIT_WATT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    ),
    CONF_REMAINING_TIME: (
        "set_remaining_time_sensor",
        sensor.sensor_schema(
            unit_of_measurement=UNIT_MINUTE,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_DURATION,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    ),
    CONF_AC_FREQUENCY: (
        "set_ac_frequency_sensor",
        sensor.sensor_schema(
            unit_of_measurement=UNIT_HERTZ,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_FREQUENCY,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
    ),
    CONF_STATUS_BYTE: (
        "set_status_byte_sensor",
        # Raw protocol diagnostics are intentionally separate from the stable
        # user-facing telemetry above.
        sensor.sensor_schema(
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    ),
    CONF_PACKET_LENGTH: (
        "set_packet_length_sensor",
        sensor.sensor_schema(
            unit_of_measurement="B",
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    ),
    CONF_PROTOCOL_ERROR_COUNT: (
        "set_protocol_error_count_sensor",
        sensor.sensor_schema(
            accuracy_decimals=0,
            state_class=STATE_CLASS_TOTAL_INCREASING,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:counter",
        ),
    ),
    CONF_CONSECUTIVE_PROTOCOL_ERRORS: (
        "set_consecutive_protocol_errors_sensor",
        sensor.sensor_schema(
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:counter",
        ),
    ),
    CONF_LAST_PROTOCOL_ERROR_UPTIME: (
        "set_last_protocol_error_uptime_sensor",
        sensor.sensor_schema(
            unit_of_measurement=UNIT_SECOND,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_DURATION,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:timer-alert-outline",
        ),
    ),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ALLPOWERS_BLE_ID): cv.use_id(AllpowersBLE),
        **{cv.Optional(key): schema for key, (_, schema) in SENSORS.items()},
    }
)


async def to_code(config):
    """Create only the optional sensor entities requested by the user."""

    parent = await cg.get_variable(config[CONF_ALLPOWERS_BLE_ID])
    for key, (setter, _) in SENSORS.items():
        if conf := config.get(key):
            sens = await sensor.new_sensor(conf)
            cg.add(getattr(parent, setter)(sens))
