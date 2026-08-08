# StopWatch Walkie UI Preview

This topic folder contains the computer-review artifacts for the round-screen
interface defined in `../DESIGN.md`.

- `assets/stopwatch-walkie-states.svg`: static six-state overview for review and annotation
- `output/index.html`: interactive single-watch preview; use A/B, arrow keys, or the on-page controls
- `assets/device/`: deterministic 466×466 PNG frames used by the UI demo firmware
- `assets/rgb565/`: simulated on-device RGB565 output
- `assets/diff/`: pixel-difference evidence between browser and RGB565 frames
- `data/visual-verdict.json`: measured visual-fidelity verdict
- `scripts/render-device-assets.mjs`: regenerate PNG frames from the HTML preview
- `scripts/embed-device-assets.mjs`: embed those PNG frames into firmware source
- `scripts/measure-device-fidelity.mjs`: regenerate RGB565 simulations, diffs, and verdict

Regenerate the complete visual pipeline from this directory:

```sh
node scripts/render-device-assets.mjs
node scripts/embed-device-assets.mjs
node scripts/measure-device-fidelity.mjs
```

The interactive HTML remains a review artifact and does not connect to Wi-Fi,
capture audio, or flash the device. The generated device PNGs are compiled only
into the `m5stack-stopwatch-ui-demo` environment; the production runtime keeps
its dynamic UI path unchanged until the visual direction is approved on-device.
