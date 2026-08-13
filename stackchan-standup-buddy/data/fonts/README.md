# fonts

设备界面用的展示字体。

- `GeistMono.ttf` —— Vercel Geist Mono 可变字体(Kimi 官网 blog 正文字体),
  [vercel/geist-font](https://github.com/vercel/geist-font),OFL 许可。
  当前倒计时/时钟数字用它离线渲染。
- `poke-digits.bin` —— 由 `tools/make-digit-font.py` 从 Geist Mono SemiBold
  渲染的 64×64 alpha 字形包(0-9 和冒号),固件直接混色绘制。
  改字体/字重后重跑脚本 + `uploadfs` 即可。
- `PokemonClassic.ttf` / `PokemonSolid.ttf` —— 宝可梦风格 fan 字体
  ([Pokemon Classic](https://www.dafont.com/pokemon-classic.font) 100% Free;
  [Pokemon Solid](https://www.dafont.com/pokemon.font) personal use),
  已被 Geist Mono 替换,留作备选。fan 字体不属于本仓库许可证覆盖范围。

