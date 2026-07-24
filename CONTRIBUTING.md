# Contributing

Contributions and hardware compatibility reports are welcome.

## Before opening an issue

1. Reproduce the problem with the latest tagged release.
2. Use the **Bug report** form for a defect in an already supported configuration.
3. Use the **Compatibility report** form for a new model or firmware family.
4. Use ESPHome `DEBUG` logs where possible.
5. Remove Wi-Fi passwords, API keys, OTA passwords, complete addresses, serial numbers and unrelated BLE data.

## Compatibility reports

A successful BLE connection alone is not enough to claim compatibility. The compatibility form requires:

- exact model, hardware revision, firmware and BLE local-name behavior;
- component revision, ESPHome version, board and framework;
- service, notification and write UUIDs with characteristic properties;
- at least one complete sanitized notification frame and its capture conditions;
- simultaneous official display/app and decoded values;
- the initial and final physical state of every control tested.

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
pip install -r requirements-lint.txt -r requirements-ci.txt
./scripts/validate.sh
```

You can also run each CI cycle independently:

```bash
./scripts/validate.sh code-quality
./scripts/validate.sh build
```

To run a single build target (same shape used by the workflow matrix):

```bash
./scripts/validate.sh build-target esp32-idf
./scripts/validate.sh build-target esp32s3-idf
./scripts/validate.sh build-target esp32-arduino
./scripts/validate.sh build-target esp32-multi-idf
```

Keep user-visible behavior documented in the README and changelog.
