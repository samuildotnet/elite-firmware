"""ESPHome external component: Solarman V5 binary push.

Pushes Modbus snapshots to ``iot.talent-monitoring.com:10000`` (or any
other Solarman-V5-speaking ingestion endpoint) using the same wire
format that the stock Deye LSW3 / WiFi-Stick dongle uses.

The frame layout (from `pysolarmanv5
<https://pysolarmanv5.readthedocs.io/en/stable/solarmanv5_protocol.html>`_)::

    +------+--------+----------+--------+--------+--------+----------+--------+------+
    | 0xA5 | length | control  | seq    | logger | frame  | modbus   | chksum | 0x15 |
    | (1)  | (2 LE) | code (2) | (2 LE) | sn (4) | type+  | rtu      | (1)    | (1)  |
    |      |        |          |        | LE     | meta   | payload  |        |      |
    +------+--------+----------+--------+--------+--------+----------+--------+------+

The ``modbus rtu payload`` is a complete function-0x03 (read holding
registers) response: ``[slave][0x03][byte_count][data...][crc16]``.
We rebuild this on-device from the raw register bytes ESPHome's
``modbus_controller`` hands us when one of our ``register_blocks``
finishes reading.

YAML usage::

    elite_solarman_v5:
      id: solarman_cloud
      server: iot.talent-monitoring.com
      port: 10000
      logger_serial: "1234567890"          # decimal, from LSW3 sticker
      modbus_controller_id: deye_modbus_controller
      modbus_address: 0x01
      push_interval: 60s
      register_blocks:
        - { start: 0x0000, count: 30 }
        - { start: 0x0258, count: 60 }
"""
from __future__ import annotations

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PORT

from esphome.components import modbus_controller

CONF_SERVER = "server"

CODEOWNERS = ["@elite-energy"]
DEPENDENCIES = ["network", "modbus_controller"]

elite_solarman_v5_ns = cg.esphome_ns.namespace("elite_solarman_v5")
EliteSolarmanV5 = elite_solarman_v5_ns.class_("EliteSolarmanV5", cg.Component)

CONF_LOGGER_SERIAL = "logger_serial"
CONF_MODBUS_CONTROLLER_ID = "modbus_controller_id"
CONF_MODBUS_ADDRESS = "modbus_address"
CONF_PUSH_INTERVAL = "push_interval"
CONF_REGISTER_BLOCKS = "register_blocks"
CONF_START = "start"
CONF_COUNT = "count"

REGISTER_BLOCK_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_START): cv.hex_uint16_t,
        # Modbus RTU caps a single read at 125 holding registers
        # (function 0x03), beyond which the inverter rejects the
        # request. Keep the per-block count under that limit.
        cv.Required(CONF_COUNT): cv.int_range(min=1, max=125),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EliteSolarmanV5),
        cv.Required(CONF_SERVER): cv.string_strict,
        cv.Optional(CONF_PORT, default=10000): cv.port,
        cv.Required(CONF_LOGGER_SERIAL): cv.string_strict,
        cv.Required(CONF_MODBUS_CONTROLLER_ID): cv.use_id(
            modbus_controller.ModbusController
        ),
        cv.Optional(CONF_MODBUS_ADDRESS, default=0x01): cv.hex_uint8_t,
        cv.Optional(
            CONF_PUSH_INTERVAL, default="60s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_REGISTER_BLOCKS, default=[]): cv.ensure_list(
            REGISTER_BLOCK_SCHEMA
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_server(config[CONF_SERVER]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_logger_serial(config[CONF_LOGGER_SERIAL]))
    cg.add(var.set_push_interval(config[CONF_PUSH_INTERVAL]))
    cg.add(var.set_modbus_address(config[CONF_MODBUS_ADDRESS]))

    parent = await cg.get_variable(config[CONF_MODBUS_CONTROLLER_ID])
    cg.add(var.set_modbus_controller(parent))

    for block in config[CONF_REGISTER_BLOCKS]:
        cg.add(var.add_register_block(block[CONF_START], block[CONF_COUNT]))

    cg.add_define("USE_ELITE_SOLARMAN_V5")
