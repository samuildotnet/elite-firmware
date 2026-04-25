"""ESPHome external component: NVS-driven provisioning.

The Elite Energy v0.2.0 firmware ships as a single binary that any
dongle can flash. Per-device configuration (identity, MQTT host,
optional WiFi creds, optional Solarman LSW3 serial) lives in the
``elite-cfg`` NVS namespace at partition offset ``0x9000`` and is
written by the backend ``/api/devices/provision`` endpoint at
provisioning time.

This component:

* Opens the ``elite-cfg`` NVS namespace at ``setup_priority::HARDWARE``
  (very early, before WiFi or any publisher), reads every key into a
  cached struct, and exposes the values via ``id(provisioning).foo()``
  lambdas.
* Asserts that ``device_id`` and ``device_token`` are present —
  without them the dongle has no MQTT identity. On a missing pair the
  component marks itself as ``failed`` so the rest of the firmware
  (MQTT publisher, Solarman publisher) gracefully short-circuits.
* Has no required YAML fields. The block is purely a placeholder that
  triggers ``setup()``::

    elite_provisioning:
      id: provisioning

NVS schema (namespace ``elite-cfg``):

==================  ========  ===========================================
Key                 Required  Default / behaviour when missing
==================  ========  ===========================================
``inverter_model``  yes       (none — ``""``)
``device_id``       yes       firmware enters AP-only fallback mode
``device_token``    yes       firmware enters AP-only fallback mode
``mqtt_host``       no        ``mqtt.elite.prygoda.xyz``
``wifi_ssid``       no        ``""``  (firmware brings up SoftAP)
``wifi_pw``         no        ``""``
``solarman_sn``     no        ``""``  (Solarman cloud publisher disabled)
==================  ========  ===========================================
"""

from __future__ import annotations

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@elite-energy"]
DEPENDENCIES = []

elite_provisioning_ns = cg.esphome_ns.namespace("elite_provisioning")
EliteProvisioning = elite_provisioning_ns.class_("EliteProvisioning", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EliteProvisioning),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add_define("USE_ELITE_PROVISIONING")
