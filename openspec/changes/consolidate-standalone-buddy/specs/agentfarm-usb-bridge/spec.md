## ADDED Requirements

### Requirement: Agent Farm trigger feed over USB-CDC
The bridge polls the Agent Farm trigger-cursor admin API on localhost and streams trigger firings to the Tab5 over USB-CDC as newline-delimited JSON, because the ESP32-P4 has no radio.

#### Scenario: live trigger firing
- **WHEN** the Agent Farm admin API reports a new trigger firing (name, type, agent, result)
- **THEN** the bridge writes one JSON line `{"t","n","ty","a","r","new":true}` to the serial port and the Tab5's SerialFeedClient parses it

#### Scenario: connect sends snapshot, not live
- **WHEN** the bridge connects to the Tab5 serial port
- **THEN** it first sends the latest history snapshot with `"new":false` for every prior firing (no pet reaction), then switches to live firings with `"new":true`

#### Scenario: heartbeat keeps device online
- **WHEN** no trigger has fired in the last interval
- **THEN** the bridge sends `{"hb":1}` so the Tab5's feed client stays marked online

### Requirement: Admin token from trigger-cursor config
The bridge reads the admin secret from the trigger-cursor `config.yaml` rather than an env var, so no secret is hardcoded.

#### Scenario: missing config
- **WHEN** the config file or `admin.secret` is absent
- **THEN** the bridge exits with a clear error message naming the expected path

### Requirement: No BLE, serial-only
The bridge uses only pyserial; it has no BLE dependency.

#### Scenario: radio-less operation
- **WHEN** the bridge runs
- **THEN** it never imports or uses bleak and does not scan for BLE devices
