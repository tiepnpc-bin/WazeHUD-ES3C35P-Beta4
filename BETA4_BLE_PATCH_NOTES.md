# Beta4 BLE reliability patch

- Separate lifecycle-control queue from HLP data queue; control events no longer compete with data during bursts.
- Prioritize lifecycle events and discard buffered old-session data on connect/disconnect.
- Recheck connection handle and notification subscription during fragmented BLE notifications.
- If the control queue itself overflows, resynchronize to latest lifecycle event.

Limitations: no ESP-IDF compile or physical hardware validation has been performed. Queue ordering during very rapid reconnects must be validated on hardware.
