# Security Policy

## Supported versions

Security fixes are provided for the latest release series only.

| Version | Supported |
| --- | --- |
| 0.2.x | Yes |
| < 0.2.0 | No |
| Development branches | Best effort |

Users should reproduce a suspected vulnerability with the latest tagged release
before reporting it. Development branches may contain incomplete or experimental
changes and are not considered stable releases.

## Reporting a vulnerability

Do not disclose a suspected vulnerability in a public issue, pull request,
discussion, log paste or BLE capture.

Use GitHub's private vulnerability reporting feature from the repository's
**Security** tab and select **Report a vulnerability**. Include enough information
to reproduce and assess the report safely:

- A concise description of the vulnerability and its expected impact.
- The affected component version or commit.
- ESPHome version, framework and ESP32 board.
- ALLPOWERS model, hardware revision and firmware version, when relevant.
- Reproduction steps or a minimal configuration.
- Sanitized logs, packet captures or protocol evidence.
- Whether physical access, BLE proximity or prior pairing is required.
- Any known workaround or mitigation.

Remove Wi-Fi credentials, API keys, OTA passwords, MAC addresses not required for
reproduction, public IP addresses and unrelated BLE traffic before submitting.

If the **Report a vulnerability** option is not available, do not publish the
technical details. Contact the maintainer through the contact options shown on
the GitHub profile and request a private reporting channel.

## Response process

This is a community-maintained project. The following targets are best-effort
rather than guaranteed service-level commitments:

- Acknowledge a complete report within 7 calendar days.
- Provide an initial assessment within 14 calendar days.
- Send progress updates at least every 14 calendar days while remediation is in
  progress.
- Coordinate disclosure after a fix or mitigation is available whenever
  practical.

A report may be closed as out of scope when it affects only upstream ESPHome,
the power-station firmware, the official mobile application or hardware outside
this repository. Where possible, the maintainer will identify the appropriate
upstream project.

## Scope

Reports are in scope when they concern code or project infrastructure maintained
in this repository, including:

- Unsafe parsing or construction of ALLPOWERS BLE protocol frames.
- Unauthorized or unintended control actions caused by this component.
- Exposure of credentials or sensitive data by repository code or workflows.
- Dependency, GitHub Actions or release-pipeline weaknesses specific to this
  repository.
- A reproducible denial of service caused by malformed BLE data when tested on
  hardware owned by the reporter.

The following are normally out of scope unless they demonstrate a separate flaw
in this repository:

- Vulnerabilities in ESPHome, Home Assistant, ESP-IDF, Arduino or third-party
  libraries.
- Vulnerabilities in ALLPOWERS firmware, hardware or official applications.
- BLE radio jamming, generic proximity attacks or lack of hardware features the
  station does not provide.
- Social engineering, credential stuffing or attacks against third-party
  services.
- Reports based only on automated scanner output without a reproducible impact.

## Safe testing expectations

Security research must be performed only on devices, networks and accounts the
reporter owns or is explicitly authorized to test.

Do not:

- Test against another person's power station, network or Home Assistant
  instance.
- Intentionally damage batteries, connected loads or charging equipment.
- Defeat electrical, thermal or battery-management protections.
- Perform destructive, high-load or unattended control tests.
- Collect, retain or publish unrelated personal data or BLE traffic.
- Disrupt GitHub, ESPHome, Home Assistant or ALLPOWERS services.

Prefer read-only reproduction first. When a control command is required, use a
safe load, remain physically present and stop immediately if the device behaves
unexpectedly.

## Disclosure

Please allow reasonable time to investigate and prepare a fix before public
disclosure. When appropriate, the project will use a GitHub security advisory to
coordinate the fix, credit the reporter and publish affected versions and
mitigations.
