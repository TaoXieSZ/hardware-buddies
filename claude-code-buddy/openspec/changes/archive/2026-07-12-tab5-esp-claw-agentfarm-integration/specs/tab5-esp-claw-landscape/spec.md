## ADDED Requirements

### Requirement: Logical landscape geometry
The ST7121 Tab5 variant SHALL expose a 1280×720 logical display to every
ESP-Claw renderer while preserving the panel's native 720×1280 MIPI-DSI video
timing.

#### Scenario: Board initializes in landscape
- **WHEN** ESP-Claw initializes the `m5stack_tab5_st7121` board
- **THEN** emote, Lua board-manager, LVGL, and Lua display consumers receive a logical width of 1280 and height of 720
- **AND** the DSI panel remains configured with physical width 720 and height 1280

#### Scenario: Device is physically oriented
- **WHEN** the Tab5 is placed horizontally with its USB/power connector on the left
- **THEN** boot graphics and the main ESP-Claw UI appear horizontally and right-side up

### Requirement: Unified panel-boundary rotation
The ST7121 implementation SHALL rotate logical RGB565 draw operations 90
degrees counter-clockwise at the common panel `draw_bitmap` boundary so every
rendering path uses the same transform.

#### Scenario: Emote renderer draws
- **WHEN** the boot or emote renderer flushes a logical rectangle
- **THEN** the corresponding pixels appear in the correct physical location and orientation

#### Scenario: LVGL draws
- **WHEN** LVGL invalidates and flushes a partial logical region
- **THEN** only the mapped physical region is updated
- **AND** the image orientation matches the emote renderer

#### Scenario: Lua display draws
- **WHEN** a Lua display script presents a full frame or dirty rectangle
- **THEN** its pixels use the same landscape coordinate system and orientation

### Requirement: Correct partial-rectangle processing
The PPA rotation path SHALL preserve the source buffer's original stride,
clip in logical space, map the clipped area into physical space, and complete
the draw before the caller can reuse the input buffer.

#### Scenario: Clipped partial update
- **WHEN** a draw rectangle extends beyond one or more logical display edges
- **THEN** the implementation clips it to 1280×720 without reading outside the source buffer
- **AND** writes only within the physical 720×1280 framebuffer

#### Scenario: Sequential animated updates
- **WHEN** renderers submit repeated partial updates during animation
- **THEN** the PPA operations are serialized
- **AND** no update observes a reused or unfinished input buffer

#### Scenario: In-place draw is requested
- **WHEN** a rotated draw uses the DSI output framebuffer itself as its input
- **THEN** the draw is rejected instead of performing an overlapping PPA operation

### Requirement: Unified landscape touch coordinates
The board touch factory SHALL convert ST7123 physical coordinates into the
same 1280×720 logical coordinate system before LVGL or Lua touch consumers
receive them.

#### Scenario: Corner mapping
- **WHEN** the user touches each physical corner with the USB connector on the left
- **THEN** the reported logical points correspond to top-left, top-right, bottom-left, and bottom-right respectively

#### Scenario: Drag direction
- **WHEN** the user drags horizontally or vertically across the landscape UI
- **THEN** the logical pointer moves in the same visual direction

#### Scenario: Controller reports an edge coordinate
- **WHEN** ST7123 reports a coordinate at or beyond its documented maximum
- **THEN** the transform clamps the input and returns a point within 0..1279 by 0..719

### Requirement: Landscape behavior survives display ownership changes
The system SHALL retain landscape geometry and orientation when display
ownership moves between emote and Lua renderers.

#### Scenario: Lua application exits
- **WHEN** a Lua UI releases the display and emote ownership is restored
- **THEN** the emote renderer redraws in 1280×720 landscape without stale portrait pixels

#### Scenario: Repeated owner switching
- **WHEN** emote and Lua ownership switch repeatedly under load
- **THEN** the system does not leak rotation resources, deadlock the draw mutex, or lose draw-completion callbacks

### Requirement: Landscape diagnostics and rollback
The firmware SHALL identify the active logical and physical geometry in boot
logs and SHALL remain recoverable by reflashing the previously proven portrait
firmware.

#### Scenario: Landscape boot log
- **WHEN** the display driver starts with software rotation enabled
- **THEN** the log reports logical 1280×720 and physical 720×1280 geometry

#### Scenario: Landscape firmware is unusable
- **WHEN** validation finds black screen, corrupt output, unusable touch, or unacceptable instability
- **THEN** the device can be restored by flashing the pre-landscape ESP-Claw image for commit `c182a77`

### Requirement: Thermal-safe default backlight
The ST7121 Tab5 variant SHALL default the LCD backlight to 40 percent so its
USB-C power path does not sustain the excessive temperature observed at 100
percent brightness.

#### Scenario: Device boots normally
- **WHEN** the `lcd_brightness` device initializes
- **THEN** its default percentage is 40
- **AND** the 10-bit LEDC duty is 409

#### Scenario: Idle thermal comparison
- **WHEN** the device runs the same idle landscape emote workload for three minutes
- **THEN** USB-C-area heating is materially lower than the 100-percent-backlight baseline
