# Capturing unsupported commands

Use an Android Bluetooth HCI snoop log to investigate settings that are not present in the public
protocol implementation, such as ECO mode or charging mode.

1. Turn off **Keep BLE Connected** in ESPHome and close other applications connected to the station.
2. Enable **Bluetooth HCI snoop log** in Android developer options.
3. Restart Bluetooth.
4. Use the official ALLPOWERS application and perform one setting change per capture.
5. Export an Android bug report and extract the BTSnoop log.
6. Open it in Wireshark and filter ATT writes:

```text
btatt.opcode == 0x12 || btatt.opcode == 0x52
```

Record the exact model, firmware, characteristic UUID, complete write payload, initial setting,
final setting and all notifications immediately following the write. Capture both directions of
every setting change.

Do not publish an entire phone bug report. It may contain traffic and metadata from unrelated
Bluetooth devices. Share only sanitized packets relevant to the power station.
