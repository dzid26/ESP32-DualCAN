# Top-level test runner for the Dorky Commander project.
#
# Targets:
#   make test          host firmware tests + webui tests (no device, no browser)
#   make test-fw       firmware native tests only
#   make test-ui       webui tests only (Playwright; spawns Vite dev server)
#   make test-device   flash + smoke the connected ESP32-C6 (panic capture)
#   make test-all      everything except test-device

.PHONY: test test-fw test-ui test-device test-all

test: test-fw test-ui

test-fw:
	@echo "==> firmware native tests"
	cd firmware && pio test -e tests-native

test-ui:
	@echo "==> webui tests (UI smoke + protocol round-trip)"
	cd webui && npx playwright test

test-device:
	@echo "==> firmware device smoke (will flash $$DORKY_PORT or /dev/ttyACM0)"
	cd firmware && ./scripts/device_smoke.sh

test-all: test test-device
