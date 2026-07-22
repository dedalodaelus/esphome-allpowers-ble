# SPDX-License-Identifier: MIT
#
# ESPHome BLE component for compatible ALLPOWERS power stations.
# BLE protocol mapping and command logic are derived from:
# https://github.com/madninjaskillz/allpowers-ble

import esphome.codegen as cg
from esphome.components import (
    ble_client,
    button as button_,
    esp32_ble_tracker,
    number as number_,
    select as select_,
    switch as switch_,
    text as text_,
)
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = [
    "sensor",
    "binary_sensor",
    "button",
    "switch",
    "number",
    "select",
    "text",
    "text_sensor",
]
DEPENDENCIES = ["ble_client"]
MULTI_CONF = True

CONF_ALLPOWERS_BLE_ID = "allpowers_ble_id"
CONF_SERVICE_UUID = "service_uuid"
CONF_STALE_TIMEOUT = "stale_timeout"
CONF_KEEPALIVE_INTERVAL = "keepalive_interval"
CONF_WATCHDOG_TIMEOUT = "watchdog_timeout"
CONF_ENABLE_SETTINGS_KEEPALIVE = "enable_settings_keepalive"
CONF_SETTINGS_KEEPALIVE_INTERVAL = "settings_keepalive_interval"
CONF_ENABLE_EXPERIMENTAL_DEVICE_NAME = "enable_experimental_device_name"

allpowers_ble_ns = cg.esphome_ns.namespace("allpowers_ble")
AllpowersBLE = allpowers_ble_ns.class_(
    "AllpowersBLE", cg.Component, ble_client.BLEClientNode
)
# Keep this scoped to match the C++ `enum class`. Without `is_class=True`,
# ESPHome would generate `allpowers_ble::AC` instead of
# `allpowers_ble::OutputType::AC`.
OutputType = allpowers_ble_ns.enum("OutputType", is_class=True)
AllpowersBLESwitch = allpowers_ble_ns.class_("AllpowersBLESwitch", switch_.Switch)
AllpowersBLEEcoSwitch = allpowers_ble_ns.class_("AllpowersBLEEcoSwitch", switch_.Switch)
AllpowersBLECarChargerSwitch = allpowers_ble_ns.class_(
    "AllpowersBLECarChargerSwitch", switch_.Switch
)
AllpowersBLESettingsKeepaliveSwitch = allpowers_ble_ns.class_(
    "AllpowersBLESettingsKeepaliveSwitch", switch_.Switch, cg.Component
)
AllpowersBLESettingsKeepaliveIntervalNumber = allpowers_ble_ns.class_(
    "AllpowersBLESettingsKeepaliveIntervalNumber", number_.Number, cg.Component
)
AllpowersBLESettingsKeepaliveButton = allpowers_ble_ns.class_(
    "AllpowersBLESettingsKeepaliveButton", button_.Button
)
AllpowersBLEEcoShutdownTimeSelect = allpowers_ble_ns.class_(
    "AllpowersBLEEcoShutdownTimeSelect", select_.Select
)
AllpowersBLEWorkModeSelect = allpowers_ble_ns.class_(
    "AllpowersBLEWorkModeSelect", select_.Select
)
AllpowersBLEDeviceNameText = allpowers_ble_ns.class_(
    "AllpowersBLEDeviceNameText", text_.Text
)


def _validate_connection_health(config):
    """Keep the refresh cadence below the forced-reconnect threshold."""

    keepalive_ms = config[CONF_KEEPALIVE_INTERVAL].total_milliseconds
    watchdog_ms = config[CONF_WATCHDOG_TIMEOUT].total_milliseconds
    if keepalive_ms < 5000:
        raise cv.Invalid("keepalive_interval must be at least 5s")
    if watchdog_ms < 10000:
        raise cv.Invalid("watchdog_timeout must be at least 10s")
    if keepalive_ms >= watchdog_ms:
        raise cv.Invalid("keepalive_interval must be shorter than watchdog_timeout")
    settings_keepalive_ms = config[CONF_SETTINGS_KEEPALIVE_INTERVAL].total_milliseconds
    if not 60000 <= settings_keepalive_ms <= 540000:
        raise cv.Invalid("settings_keepalive_interval must be between 1min and 9min")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AllpowersBLE),
            cv.Optional(CONF_SERVICE_UUID, default="FFF0"): esp32_ble_tracker.bt_uuid,
            cv.Optional(
                CONF_STALE_TIMEOUT, default="30s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_KEEPALIVE_INTERVAL, default="20s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_WATCHDOG_TIMEOUT, default="45s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_ENABLE_SETTINGS_KEEPALIVE, default=False): cv.boolean,
            cv.Optional(
                CONF_SETTINGS_KEEPALIVE_INTERVAL, default="9min"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_ENABLE_EXPERIMENTAL_DEVICE_NAME, default=False
            ): cv.boolean,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(ble_client.BLE_CLIENT_SCHEMA),
    _validate_connection_health,
)


def _set_service_uuid(var, uuid):
    """Select the matching C++ setter for a validated 16/32/128-bit UUID."""

    # ESPHome validates the accepted textual UUID formats before codegen. The
    # generated C++ API uses separate overloads because each width needs a
    # different conversion helper.
    if len(uuid) == len(esp32_ble_tracker.bt_uuid16_format):
        cg.add(var.set_service_uuid16(esp32_ble_tracker.as_hex(uuid)))
    elif len(uuid) == len(esp32_ble_tracker.bt_uuid32_format):
        cg.add(var.set_service_uuid32(esp32_ble_tracker.as_hex(uuid)))
    elif len(uuid) == len(esp32_ble_tracker.bt_uuid128_format):
        uuid128 = esp32_ble_tracker.as_reversed_hex_array(uuid)
        cg.add(var.set_service_uuid128(uuid128))


async def to_code(config):
    """Register the BLE node and emit protocol-level component settings."""

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)

    _set_service_uuid(var, config[CONF_SERVICE_UUID])
    cg.add(var.set_stale_timeout(config[CONF_STALE_TIMEOUT].total_milliseconds))
    cg.add(
        var.set_keepalive_interval(config[CONF_KEEPALIVE_INTERVAL].total_milliseconds)
    )
    cg.add(var.set_watchdog_timeout(config[CONF_WATCHDOG_TIMEOUT].total_milliseconds))
    cg.add(var.set_settings_keepalive_enabled(config[CONF_ENABLE_SETTINGS_KEEPALIVE]))
    cg.add(
        var.set_settings_keepalive_interval(
            config[CONF_SETTINGS_KEEPALIVE_INTERVAL].total_milliseconds
        )
    )
    cg.add(
        var.set_experimental_device_name_enabled(
            config[CONF_ENABLE_EXPERIMENTAL_DEVICE_NAME]
        )
    )
