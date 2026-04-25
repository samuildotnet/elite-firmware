// Host-test shim for ESPHome's log.h. The production header maps to
// ESP-IDF's logging macros; the host build is guarded by USE_ESP_IDF
// so these never get evaluated, but they need to exist so the header
// parses cleanly.
#pragma once

#define ESP_LOGE(tag, ...) ((void) 0)
#define ESP_LOGW(tag, ...) ((void) 0)
#define ESP_LOGI(tag, ...) ((void) 0)
#define ESP_LOGD(tag, ...) ((void) 0)
#define ESP_LOGV(tag, ...) ((void) 0)
#define ESP_LOGCONFIG(tag, ...) ((void) 0)
