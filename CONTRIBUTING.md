# Contributing

Contributions and hardware compatibility reports are welcome.

## Before opening an issue

1. Reproduce the problem with the latest tagged release.
2. Use ESPHome `DEBUG` logs where possible.
3. Remove Wi-Fi passwords, API keys, OTA passwords, public addresses and unrelated BLE data.
4. Include the exact ALLPOWERS model, hardware revision, firmware version and BLE local name.
5. State whether you verified telemetry only or also tested AC, DC and light control.

## Compatibility reports

A successful BLE connection alone is not enough to claim compatibility. Include:

- GATT service and characteristic UUIDs
- At least one sanitized status notification
- Values shown simultaneously by the official application or device display
- Results of each output control tested

## Protocol changes

Do not add guessed commands. New fields or controls should include:

- A sanitized BLE capture from the official application
- Exact model and firmware
- Initial and final application setting
- Characteristic UUID and complete payload
- Confirmation from repeated captures
- Regression tests

## Comments and maintainability

Comments should explain facts that cannot be recovered safely from the code
alone, especially:

- Provenance and confidence level of protocol fields
- GATT setup and connection-state transitions
- Safety invariants around stale telemetry and combined output writes
- Differences between ESPHome entity APIs, such as unknown numeric and binary
  sensor states
- Non-obvious code-generation constraints in the Python modules

Avoid comments that merely restate an assignment, null check or obvious schema
declaration. Prefer named constants and small helper functions when they make
the protocol easier to understand without additional prose.

## Pull requests

Run:

```bash
pip install -r requirements-lint.txt
python -m compileall -q components tests
ruff check components tests
ruff format --check components tests
flake8 components tests
pylint components/allpowers_ble tests/test_protocol.py
clang-format --dry-run --Werror \
  components/allpowers_ble/allpowers_ble.h \
  components/allpowers_ble/allpowers_ble.cpp
cpplint \
  components/allpowers_ble/allpowers_ble.h \
  components/allpowers_ble/allpowers_ble.cpp
python tests/test_protocol.py
esphome config tests/ci.yaml
esphome compile tests/ci.yaml
```

Keep user-visible behavior documented in the README and changelog.
