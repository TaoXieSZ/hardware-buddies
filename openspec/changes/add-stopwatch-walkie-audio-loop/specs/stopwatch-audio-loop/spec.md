## Purpose

Define a testable end-to-end push-to-talk audio loop between an M5 StopWatch and a Mac bridge so the hardware audio path can be proven before agent-control features are added.

## ADDED Requirements

### Requirement: Push-to-talk bounds every utterance
The StopWatch SHALL start a new utterance only when KEYA is pressed while the bridge is connected, SHALL capture audio only while KEYA remains held, and SHALL finalize that utterance when KEYA is released.

#### Scenario: Hold KEYA to record
- **WHEN** the device is ready and the user presses and holds KEYA
- **THEN** the device SHALL enter the recording state, start one new utterance, and stream microphone samples until KEYA is released

#### Scenario: Release KEYA to submit
- **WHEN** the user releases KEYA during an active utterance
- **THEN** the device SHALL stop capturing audio, finalize the same utterance, provide a short tactile acknowledgement, and show that transcription is in progress

#### Scenario: KEYA pressed while disconnected
- **WHEN** the user presses KEYA while no bridge connection is available
- **THEN** the device SHALL NOT capture or retain an utterance and SHALL show a recoverable connection error

### Requirement: KEYB cancels the active utterance
The StopWatch SHALL let the user cancel an in-progress utterance with KEYB, and the bridge MUST discard all buffered audio for the cancelled utterance without submitting it for transcription.

#### Scenario: Cancel during recording
- **WHEN** the user presses KEYB while KEYA recording is active
- **THEN** capture SHALL stop, the active utterance SHALL be marked cancelled, no ASR request SHALL be made for it, and the device SHALL return to the ready state

### Requirement: Audio exchange is framed and correlated
The device and bridge SHALL exchange one active utterance at a time over WebSocket. Each utterance MUST have a unique correlation identifier, MUST declare its audio format before binary audio frames, and MUST be explicitly ended or cancelled.

#### Scenario: Complete framed utterance
- **WHEN** the bridge receives an utterance start message, ordered binary audio frames, and a matching utterance end message
- **THEN** it SHALL assemble exactly those frames into one transcription input associated with the declared identifier

#### Scenario: Invalid frame sequence
- **WHEN** the bridge receives binary audio without an active utterance, a mismatched identifier, or a second start before the first utterance ends
- **THEN** it SHALL reject the invalid sequence, discard the affected partial audio, and return a structured recoverable error

### Requirement: Captured audio uses the agreed PCM format
Every submitted utterance SHALL contain signed 16-bit little-endian mono PCM sampled at 16 kHz. The bridge MUST reject a declared or decoded format that does not match this contract.

#### Scenario: Valid PCM format
- **WHEN** the device starts an utterance with 16 kHz, signed 16-bit, little-endian, mono PCM and sends aligned binary frames
- **THEN** the bridge SHALL accept and buffer the frames without resampling

#### Scenario: Unsupported audio format
- **WHEN** an utterance declares an unsupported sample rate, sample width, byte order, channel count, or contains a partial sample
- **THEN** the bridge SHALL discard it and return a structured audio-format error

### Requirement: Completed utterances are transcribed and returned
For each valid completed utterance, the bridge SHALL submit a complete audio input to DashScope `qwen3-asr-flash` and SHALL return either the recognized text or a structured failure correlated to the same utterance identifier.

#### Scenario: Successful transcription
- **WHEN** DashScope returns recognized text for a valid completed utterance
- **THEN** the bridge SHALL send the text with the matching identifier and the StopWatch SHALL display it as the result

#### Scenario: No speech recognized
- **WHEN** DashScope completes successfully but returns no recognized speech
- **THEN** the bridge SHALL return a no-speech result and the StopWatch SHALL display a retryable message rather than an empty result screen

#### Scenario: ASR request fails
- **WHEN** DashScope is unavailable, rejects the request, times out, or returns an invalid response
- **THEN** the bridge SHALL return a structured failure without exposing credentials and the StopWatch SHALL display a recoverable transcription error

### Requirement: Device state remains observable and recoverable
The StopWatch SHALL visibly distinguish connecting, ready, recording, transcribing, result, and error states. A recoverable error SHALL NOT require a firmware restart before another attempt.

#### Scenario: Normal state progression
- **WHEN** a connected user records and successfully transcribes an utterance
- **THEN** the visible state SHALL progress from ready to recording to transcribing to result, then allow the next KEYA press to start a new utterance

#### Scenario: Connection lost during an utterance
- **WHEN** the WebSocket connection closes before the active utterance completes
- **THEN** the device and bridge SHALL discard the partial utterance, the device SHALL show a connection error, and the device SHALL return to ready after reconnecting

### Requirement: Audio and credentials remain scoped to the prototype
The device MUST transmit microphone audio only for an active KEYA-bounded utterance. DashScope credentials MUST remain on the Mac, MUST be loaded from local configuration or environment, and MUST NOT be sent to the device, logged, or committed to source control.

#### Scenario: Device is idle
- **WHEN** KEYA is not held and no utterance is active
- **THEN** the device SHALL NOT capture, buffer, or transmit microphone audio

#### Scenario: Bridge starts without credentials
- **WHEN** the bridge starts without a usable DashScope credential
- **THEN** it SHALL report a local configuration error without printing a secret value and SHALL NOT accept an utterance for cloud transcription

### Requirement: Audio loop survives repeated use
The prototype SHALL complete repeated push-to-talk cycles without rebooting the StopWatch, restarting the bridge, mixing utterances, or leaving either endpoint permanently outside a recoverable state.

#### Scenario: Repeated hardware smoke run
- **WHEN** a tester performs 20 consecutive utterances of at least five seconds each on the same device and bridge process
- **THEN** every utterance SHALL end in a correlated result or structured recoverable error, with no device reboot, bridge restart, or audio attributed to another utterance
