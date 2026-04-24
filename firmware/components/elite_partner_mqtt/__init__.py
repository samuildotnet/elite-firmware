"""ESPHome external component: secondary MQTT-TLS publisher.

Publishes the same sensor state stream that the stock `mqtt:` component
publishes, but to a *second* broker (the partner ingestion server). The
broker is configured for anonymous TLS — no username / password.

YAML usage::

    elite_partner_mqtt:
      id: partner_mqtt
      broker: 95.60.174.157
      port: 8883
      username: ""          # anonymous
      password: ""
      client_id: my-dongle-partner
      topic_prefix: v1/t/elite/s/home/i/deye-12k-01
      publish_interval: 30s

The component plugs into ESPHome's global sensor registry — every sensor
that the device exposes via the stock `sensor:` blocks is republished to
the partner broker on every state change.
"""
from __future__ import annotations

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_BROKER,
    CONF_PORT,
    CONF_USERNAME,
    CONF_PASSWORD,
    CONF_CLIENT_ID,
    CONF_TOPIC_PREFIX,
)

CODEOWNERS = ["@elite-energy"]
DEPENDENCIES = ["network"]
AUTO_LOAD = ["json"]

elite_partner_mqtt_ns = cg.esphome_ns.namespace("elite_partner_mqtt")
ElitePartnerMqtt = elite_partner_mqtt_ns.class_("ElitePartnerMqtt", cg.Component)

CONF_PUBLISH_INTERVAL = "publish_interval"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ElitePartnerMqtt),
        cv.Required(CONF_BROKER): cv.string_strict,
        cv.Optional(CONF_PORT, default=8883): cv.port,
        cv.Optional(CONF_USERNAME, default=""): cv.string,
        cv.Optional(CONF_PASSWORD, default=""): cv.string,
        cv.Optional(CONF_CLIENT_ID, default=""): cv.string,
        cv.Required(CONF_TOPIC_PREFIX): cv.string_strict,
        cv.Optional(CONF_PUBLISH_INTERVAL, default="30s"): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_broker(config[CONF_BROKER]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_username(config[CONF_USERNAME]))
    cg.add(var.set_password(config[CONF_PASSWORD]))
    cg.add(var.set_client_id(config[CONF_CLIENT_ID]))
    cg.add(var.set_topic_prefix(config[CONF_TOPIC_PREFIX]))
    cg.add(var.set_publish_interval(config[CONF_PUBLISH_INTERVAL]))

    # Pull in the IDF mqtt component. esp-mqtt is bundled with ESP-IDF
    # 5.x as a kconfig-selected component, so no explicit dependency.
    cg.add_define("USE_ELITE_PARTNER_MQTT")
