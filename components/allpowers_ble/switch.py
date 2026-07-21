# SPDX-License-Identifier: MIT
#
# ESPHome BLE component for compatible ALLPOWERS power stations.
# BLE protocol mapping and command logic are derived from:
# https://github.com/madninjaskillz/allpowers-ble

import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv

from . import (
    CONF_ALLPOWERS_BLE_ID,
    AllpowersBLE,
    AllpowersBLEEcoSwitch,
    AllpowersBLESwitch,
    OutputType,
)

CONF_AC_OUTPUT = "ac_output"
CONF_DC_OUTPUT = "dc_output"
CONF_ECO_MODE = "eco_mode"
CONF_LIGHT = "light"

# OutputType selects one bit in the combined protocol command; the named
# parent setter also lets the C++ component publish confirmed/optimistic state
# back to the same ESPHome switch instance.
SWITCHES = {
    CONF_AC_OUTPUT: (OutputType.AC, "set_ac_switch"),
    CONF_DC_OUTPUT: (OutputType.DC, "set_dc_switch"),
    CONF_LIGHT: (OutputType.LIGHT, "set_light_switch"),
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ALLPOWERS_BLE_ID): cv.use_id(AllpowersBLE),
        **{
            cv.Optional(key): switch.switch_schema(
                AllpowersBLESwitch,
                # Inversion would make the displayed state disagree with the
                # protocol bitmap. Restoring a switch at boot is also unsafe
                # before the first complete status frame has been received.
                block_inverted=True,
                default_restore_mode="DISABLED",
            )
            for key in SWITCHES
        },
        cv.Optional(CONF_ECO_MODE): switch.switch_schema(
            AllpowersBLEEcoSwitch,
            # ECO writes are only safe after a complete settings notification
            # has supplied the fields that must be preserved in the command.
            block_inverted=True,
            default_restore_mode="DISABLED",
        ),
    }
)


async def to_code(config):
    """Create output and ECO switches with their distinct safety models."""

    parent = await cg.get_variable(config[CONF_ALLPOWERS_BLE_ID])
    for key, (output_type, parent_setter) in SWITCHES.items():
        if conf := config.get(key):
            var = await switch.new_switch(conf)
            cg.add(var.set_parent(parent))
            cg.add(var.set_output_type(output_type))
            cg.add(getattr(parent, parent_setter)(var))

    if conf := config.get(CONF_ECO_MODE):
        var = await switch.new_switch(conf)
        cg.add(var.set_parent(parent))
        cg.add(parent.set_eco_switch(var))
