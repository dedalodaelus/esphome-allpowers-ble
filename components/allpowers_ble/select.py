# SPDX-License-Identifier: MIT
#
# ESPHome BLE component for compatible ALLPOWERS power stations.
# BLE protocol mapping and command logic are derived from:
# https://github.com/madninjaskillz/allpowers-ble

import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG, ICON_TIMER

from . import (
    CONF_ALLPOWERS_BLE_ID,
    AllpowersBLE,
    AllpowersBLEEcoShutdownTimeSelect,
    AllpowersBLEWorkModeSelect,
)

CONF_ECO_SHUTDOWN_TIME = "eco_shutdown_time"
CONF_WORK_MODE = "work_mode"

# The R600 application exposes these four protocol values in this order. The
# C++ index-to-hour mapping must remain aligned with this list so codegen does
# not need to parse localized display strings at runtime.
ECO_SHUTDOWN_TIME_OPTIONS = ["1 hour", "2 hours", "4 hours", "6 hours"]

# These labels and their numeric order match the official application. C++
# operates on the protocol values 0, 1 and 2, avoiding string comparisons in
# the BLE write path.
WORK_MODE_OPTIONS = ["Mute Mode", "Standard Mode", "Fast Mode"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ALLPOWERS_BLE_ID): cv.use_id(AllpowersBLE),
        cv.Optional(CONF_ECO_SHUTDOWN_TIME): select.select_schema(
            AllpowersBLEEcoShutdownTimeSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon=ICON_TIMER,
        ),
        cv.Optional(CONF_WORK_MODE): select.select_schema(
            AllpowersBLEWorkModeSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
    }
)


async def to_code(config):
    """Create settings selects backed by the shared read-modify-write state."""

    parent = await cg.get_variable(config[CONF_ALLPOWERS_BLE_ID])
    if conf := config.get(CONF_ECO_SHUTDOWN_TIME):
        var = await select.new_select(conf, options=ECO_SHUTDOWN_TIME_OPTIONS)
        cg.add(var.set_parent(parent))
        cg.add(parent.set_eco_shutdown_time_select(var))

    if conf := config.get(CONF_WORK_MODE):
        var = await select.new_select(conf, options=WORK_MODE_OPTIONS)
        cg.add(var.set_parent(parent))
        cg.add(parent.set_work_mode_select(var))
