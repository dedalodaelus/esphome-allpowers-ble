# SPDX-License-Identifier: MIT
#
# ESPHome BLE component for compatible ALLPOWERS power stations.
# BLE protocol mapping and command logic are derived from:
# https://github.com/madninjaskillz/allpowers-ble

import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG, ICON_TIMER, UNIT_MINUTE

from . import (
    CONF_ALLPOWERS_BLE_ID,
    AllpowersBLE,
    AllpowersBLESettingsKeepaliveIntervalNumber,
)

CONF_SETTINGS_KEEPALIVE_INTERVAL = "settings_keepalive_interval"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ALLPOWERS_BLE_ID): cv.use_id(AllpowersBLE),
        cv.Optional(CONF_SETTINGS_KEEPALIVE_INTERVAL): number.number_schema(
            AllpowersBLESettingsKeepaliveIntervalNumber,
            unit_of_measurement=UNIT_MINUTE,
            icon=ICON_TIMER,
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
    }
)


async def to_code(config):
    """Create the persistent settings-keepalive interval control."""

    parent = await cg.get_variable(config[CONF_ALLPOWERS_BLE_ID])
    if conf := config.get(CONF_SETTINGS_KEEPALIVE_INTERVAL):
        var = await number.new_number(conf, parent, min_value=1, max_value=9, step=1)
        await cg.register_component(var, conf)
