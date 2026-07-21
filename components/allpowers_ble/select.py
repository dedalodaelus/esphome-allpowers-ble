# SPDX-License-Identifier: MIT
#
# ESPHome BLE component for compatible ALLPOWERS power stations.
# BLE protocol mapping and command logic are derived from:
# https://github.com/madninjaskillz/allpowers-ble

import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_CONFIG, ICON_TIMER

from . import CONF_ALLPOWERS_BLE_ID, AllpowersBLE, AllpowersBLEEcoShutdownTimeSelect

CONF_ECO_SHUTDOWN_TIME = "eco_shutdown_time"

# The R600 application exposes these four protocol values in this order. The
# C++ index-to-hour mapping must remain aligned with this list so codegen does
# not need to parse localized display strings at runtime.
ECO_SHUTDOWN_TIME_OPTIONS = ["1 hour", "2 hours", "4 hours", "6 hours"]

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ALLPOWERS_BLE_ID): cv.use_id(AllpowersBLE),
        cv.Optional(CONF_ECO_SHUTDOWN_TIME): select.select_schema(
            AllpowersBLEEcoShutdownTimeSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon=ICON_TIMER,
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
