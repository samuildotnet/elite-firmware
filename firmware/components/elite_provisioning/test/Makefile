# Host-side unit tests for elite_provisioning's NVS parsing layer.
#
# Run with:
#   make -C firmware/components/elite_provisioning/test
#
# CI invokes this as a standalone job (no ESPHome / ESP-IDF toolchain
# required). The production .cpp file is compiled with USE_ESP_IDF
# *unset*, which the source guards against — the only TU exposed to
# the host compiler is the pure parsing layer.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror

# Reach the parent component directory and stub out the ESPHome core
# includes (component.h, log.h) with a minimal shim — the parsing
# layer doesn't actually use ``Component`` at runtime, only the type
# is referenced by the header.
CPPFLAGS = -I. -I.. -Ishims

BIN := elite_provisioning_tests

.PHONY: all clean
all: run

$(BIN): test_elite_provisioning.cpp ../elite_provisioning.cpp shims/esphome/core/component.h shims/esphome/core/log.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ test_elite_provisioning.cpp ../elite_provisioning.cpp

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)
