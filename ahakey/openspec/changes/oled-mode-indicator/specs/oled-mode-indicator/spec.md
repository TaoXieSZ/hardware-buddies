## ADDED Requirements

### Requirement: Every mode's OLED shows a mode marker

Each work mode's OLED output SHALL carry a persistent, fixed-position marker identifying the mode (0 / 1 / 2), so the active mode is recognizable at a glance and a switch is confirmable on the device.

#### Scenario: Switching modes is visible on the OLED
- **WHEN** the user switches the keyboard from one mode to another
- **THEN** the OLED shows the destination mode's frames, and those frames include a marker that distinguishes that mode from the other two

#### Scenario: Marker survives a custom image
- **WHEN** a user uploads their own GIF for a mode
- **THEN** the encoded frames still carry that mode's marker (the marker is applied at encode time, not only baked into the factory assets)

### Requirement: Marker is legible at the OLED's real format

The marker SHALL be designed for the confirmed OLED format — 160×80, RGB565 color — so it reads clearly without depending on the underlying art.

#### Scenario: Marker readable on a busy frame
- **WHEN** the underlying frame is visually busy or dark/light at the marker's location
- **THEN** the marker remains legible (e.g. via a contrasting backing or a reserved region), not lost in the image

### Requirement: Every mode has a default image

Each of Mode 0, 1, and 2 SHALL have a default OLED image, so no mode is blank.

#### Scenario: Mode 2 is no longer empty
- **WHEN** the device is in Mode 2 with no user customization
- **THEN** the OLED shows a default Mode 2 image bearing the Mode 2 marker

### Requirement: Editor preview matches device output

The OLED preview in the editor SHALL show the same marker the device will display, so the user can verify before writing to hardware.

#### Scenario: Preview shows the marker
- **WHEN** the user previews a mode's OLED in the editor
- **THEN** the preview renders the mode marker in the same position/style as the encoded frames sent to the device
