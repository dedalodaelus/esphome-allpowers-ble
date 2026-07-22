# SPDX-License-Identifier: MIT
#
# ESPHome BLE component for compatible ALLPOWERS power stations.
# BLE protocol mapping and command logic are derived from:
# https://github.com/madninjaskillz/allpowers-ble

import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG, ICON_TIMER

from . import CONF_ALLPOWERS_BLE_ID, AllpowersBLE, AllpowersBLESettingsKeepaliveButton

CONF_SEND_SETTINGS_KEEPALIVE = "send_settings_keepalive"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ALLPOWERS_BLE_ID): cv.use_id(AllpowersBLE),
        cv.Optional(CONF_SEND_SETTINGS_KEEPALIVE): button.button_schema(
            AllpowersBLESettingsKeepaliveButton,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon=ICON_TIMER,
        ),
    }
)


async def to_code(config):
    """Create the one-shot settings keepalive button."""

    parent = await cg.get_variable(config[CONF_ALLPOWERS_BLE_ID])
    if conf := config.get(CONF_SEND_SETTINGS_KEEPALIVE):
        await button.new_button(conf, parent)
