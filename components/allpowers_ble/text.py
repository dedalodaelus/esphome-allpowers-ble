# SPDX-License-Identifier: MIT
#
# Experimental writable device-name entity for compatible ALLPOWERS stations.

import esphome.codegen as cg
from esphome.components import text
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG

from . import CONF_ALLPOWERS_BLE_ID, AllpowersBLE, AllpowersBLEDeviceNameText

CONF_DEVICE_NAME = "device_name"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ALLPOWERS_BLE_ID): cv.use_id(AllpowersBLE),
        cv.Optional(CONF_DEVICE_NAME): text.text_schema(
            AllpowersBLEDeviceNameText,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:rename-box",
            mode="TEXT",
        ),
    }
)


async def to_code(config):
    """Create a non-optimistic text input confirmed by command-0x35 responses."""

    parent = await cg.get_variable(config[CONF_ALLPOWERS_BLE_ID])
    if conf := config.get(CONF_DEVICE_NAME):
        var = await text.new_text(conf, min_length=1, max_length=96)
        cg.add(var.set_parent(parent))
        cg.add(parent.set_device_name_text(var))
