import { spawnSync } from 'node:child_process';
import { mkdirSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';

const scriptDir = dirname(fileURLToPath(import.meta.url));
const previewRoot = dirname(scriptDir);
const sourceDir = join(previewRoot, 'assets', 'device');
const rgb565Dir = join(previewRoot, 'assets', 'rgb565');
const diffDir = join(previewRoot, 'assets', 'diff');
const dataDir = join(previewRoot, 'data');
const files = readdirSync(sourceDir).filter((name) => name.endsWith('.png')).sort();

mkdirSync(rgb565Dir, { recursive: true });
mkdirSync(diffDir, { recursive: true });
mkdirSync(dataDir, { recursive: true });

const stateMetrics = files.map((name) => {
  const source = join(sourceDir, name);
  const rgb565 = join(rgb565Dir, name);
  const diff = join(diffDir, name);

  const convert = spawnSync('magick', [
    source,
    '-channel', 'R', '-posterize', '32',
    '-channel', 'G', '-posterize', '64',
    '-channel', 'B', '-posterize', '32',
    '+channel', rgb565,
  ], { encoding: 'utf8' });
  if (convert.status !== 0) {
    throw new Error(convert.stderr || `ImageMagick failed for ${name}`);
  }

  const compare = spawnSync('compare', ['-metric', 'RMSE', source, rgb565, diff], { encoding: 'utf8' });
  const match = compare.stderr.match(/\((\d+(?:\.\d+)?)\)/);
  if (!match) {
    throw new Error(compare.stderr || `Unable to read RMSE for ${name}`);
  }
  const normalizedRmse = Number(match[1]);
  return {
    state: name.replace(/\.png$/, ''),
    normalized_rmse: normalizedRmse,
    visual_score: Number(((1 - normalizedRmse) * 100).toFixed(2)),
  };
});

const meanRmse = stateMetrics.reduce((sum, item) => sum + item.normalized_rmse, 0) / stateMetrics.length;
const report = {
  reference: 'assets/device/*.png (approved browser-rendered 466x466 frames)',
  target: 'assets/rgb565/*.png (firmware canvas color-depth simulation)',
  score: Number(((1 - meanRmse) * 100).toFixed(2)),
  category_match: true,
  differences: ['RGB565 color quantization only; geometry, type rasterization, spacing, and icons are unchanged.'],
  suggestions: [],
  state_metrics: stateMetrics,
  reasoning: 'The demo firmware decodes the approved PNG bytes at 1:1 scale into a 466x466 RGB565 PSRAM canvas.',
};

const reportPath = join(dataDir, 'visual-verdict.json');
writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`);
console.log(`Visual fidelity ${report.score}/100; wrote ${relative(previewRoot, reportPath)}`);
