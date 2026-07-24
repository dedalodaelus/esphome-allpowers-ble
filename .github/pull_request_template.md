## Summary

<!-- Explain what changed and why. Keep the pull request focused. -->

Closes #

## Change type

<!-- Check every applicable item. -->

- [ ] Bug fix
- [ ] New or changed feature
- [ ] Protocol or BLE behavior
- [ ] Refactor with no intended user-visible behavior change
- [ ] Documentation
- [ ] CI, tooling or dependency update

## Test evidence

<!-- List the commands run and summarize their results. Attach relevant logs. -->

- [ ] `./scripts/validate.sh code-quality`
- [ ] `./scripts/validate.sh build`
- [ ] Relevant native or Python regression tests were added or updated
- [ ] The change was tested on hardware, or hardware testing is not applicable

Commands and results:

```text

```

## Hardware and firmware

<!-- Required for compatibility, BLE, protocol or entity-behavior changes. -->

- ALLPOWERS model:
- Hardware revision:
- Firmware version:
- BLE local name:
- ESP32 board:
- ESPHome version:
- Framework: ESP-IDF / Arduino / not applicable

## Protocol evidence

<!-- Required for new fields, commands, settings or compatibility claims. -->

- [ ] The change is not based on guessed protocol behavior
- [ ] Sanitized BLE captures or equivalent evidence are attached or linked
- [ ] Service and characteristic UUIDs are documented
- [ ] Initial and final device/application states are documented
- [ ] Repeated captures confirm the behavior
- [ ] Unknown bits and fields remain preserved where applicable

Evidence or explanation when not applicable:

```text

```

## Safety and compatibility

- [ ] I considered stale telemetry, reconnects and failed GATT operations
- [ ] Combined output writes preserve unrelated output states
- [ ] Experimental or unverified controls remain clearly gated and documented
- [ ] Logs and captures contain no credentials or unrelated private data
- [ ] Backward-compatibility or migration effects are described below

Compatibility or migration notes:

<!-- State whether entity names, IDs, YAML keys, defaults or behavior change. -->

## Documentation and changelog

- [ ] `CHANGELOG.md` contains an entry for this change
- [ ] README, examples and protocol documentation were updated when required
- [ ] A migration note was added for user-visible breaking changes, or none is required

## Reviewer notes

<!-- Highlight non-obvious tradeoffs, residual risks or areas needing special review. -->
