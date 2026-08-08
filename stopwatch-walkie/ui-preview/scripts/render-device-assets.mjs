import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, rmSync, statSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const scriptDir = dirname(fileURLToPath(import.meta.url));
const previewRoot = dirname(scriptDir);
const htmlPath = join(previewRoot, 'output', 'index.html');
const outputDir = join(previewRoot, 'assets', 'device');
const chrome = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const stateNames = ['connecting', 'ready', 'recording', 'transcribing', 'result', 'error'];
mkdirSync(outputDir, { recursive: true });

const delay = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));

async function renderState(name, state) {
  const profileDir = mkdtempSync(join(tmpdir(), `stopwatch-ui-${name}-`));
  const output = join(outputDir, `${name}.png`);
  const url = `${pathToFileURL(htmlPath).href}?device=1&state=${state}`;
  rmSync(output, { force: true });

  const child = spawn(chrome, [
      '--headless=new',
      '--disable-gpu',
      '--disable-background-networking',
      '--disable-component-update',
      '--disable-extensions',
      '--disable-sync',
      '--hide-scrollbars',
      '--force-device-scale-factor=1',
      '--no-first-run',
      '--no-default-browser-check',
      `--user-data-dir=${profileDir}`,
      '--window-size=466,466',
      `--screenshot=${output}`,
      url,
  ], { detached: true, stdio: 'ignore' });

  try {
    const deadline = Date.now() + 20_000;
    while (Date.now() < deadline) {
      if (existsSync(output) && statSync(output).size > 0) {
        return;
      }
      if (child.exitCode !== null) {
        throw new Error(`Chrome exited before rendering ${name} (code ${child.exitCode})`);
      }
      await delay(100);
    }
    throw new Error(`Timed out rendering ${name}`);
  } finally {
    if (child.exitCode === null) {
      try {
        process.kill(-child.pid, 'SIGTERM');
      } catch {
        // Chrome may have completed between the status check and termination.
      }
    }
    await delay(300);
    if (child.exitCode === null) {
      try {
        process.kill(-child.pid, 'SIGKILL');
      } catch {
        // Chrome may have completed while waiting for graceful shutdown.
      }
      await delay(100);
    }
    rmSync(profileDir, { recursive: true, force: true, maxRetries: 5, retryDelay: 100 });
  }
}

for (const [state, name] of stateNames.entries()) {
  await renderState(name, state);
}

console.log(`Rendered ${stateNames.length} device assets in ${outputDir}`);
