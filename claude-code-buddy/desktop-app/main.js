// Electron shell — hosts a 320×240 logical canvas scaled 3× for macOS.
const { app, BrowserWindow } = require('electron');
const path = require('path');

const SCALE = 3;
const WIDTH = 320 * SCALE;
const HEIGHT = 240 * SCALE;

function createWindow() {
  const win = new BrowserWindow({
    width: WIDTH,
    height: HEIGHT,
    useContentSize: true,
    resizable: false,
    title: 'StackChan',
    backgroundColor: '#000000',
    webPreferences: {
      contextIsolation: true,
      nodeIntegration: false,
    },
  });
  win.setMenuBarVisibility(false);
  win.loadFile(path.join(__dirname, 'renderer', 'index.html'));
}

app.whenReady().then(createWindow);
app.on('window-all-closed', () => app.quit());
