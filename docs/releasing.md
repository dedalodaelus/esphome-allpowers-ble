# Release process

This project publishes immutable Git tags without a `v` prefix, for example `0.2.1`. Stable documentation and examples must reference the exact release number. The moving `main` branch is development-only.

## Preparing a release

1. Confirm that `CHANGELOG.md` contains all user-visible changes under `Unreleased`.
2. Confirm the minimum supported ESPHome version and the validated station models, hardware revisions and firmware versions.
3. Review `docs/migration.md`, known limitations and rollback guidance.
4. Set the stable references in `README.md`, `examples/minimal.yaml` and `examples/bluetooth-proxy.yaml` to the release being prepared. Keep the component and package references identical.
5. Run the repository validation command and compile every stable example from the exact release commit.
6. Compile `examples/development-main.yaml` separately to ensure the documented development path still resolves.
7. Create and sign the release commit and tag using the exact release number, without a `v` prefix.
8. Publish release notes containing the minimum ESPHome version, validated hardware/firmware scope, known limitations, migration instructions and rollback instructions.
9. For every generated archive attached to the release, publish a SHA-256 checksum. Only claim SBOM or provenance coverage for artifacts and inputs that are actually represented.

## Verification before publication

- [ ] Stable examples resolve only the exact release tag and never fall back to `main`.
- [ ] External-component and package references use the same release number.
- [ ] The stable examples compile with the minimum supported ESPHome version.
- [ ] The stable examples compile with the latest supported ESPHome version.
- [ ] The development example resolves from `main` and is clearly marked as unstable.
- [ ] Upgrade and rollback between the previous and new tags have been tested.
- [ ] Release notes link to migration and known-limitation documentation.
- [ ] Attached generated artifacts have verified SHA-256 checksums.

## User upgrade and rollback

To upgrade, change every repository reference to the same newer release number, review the release notes and migration guide, compile, and test telemetry and controls on the exact station model and firmware before restoring unattended automations.

To roll back, restore the previous release number in every repository reference and compile again. Do not mix component code from one release with the package from another.
