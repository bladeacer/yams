# yams

![Screenshot](preview.webp)

Yet another media screensaver.
> Or status indicator

`playerctl` wrapper go brr.

## Features

- CJK/Unicode Support (`ncursesw`)
- Currently Playing Media (`playerctl`)
  - Metadata (Media name/title/etc.)
  - Album art rendered as ASCII/braille art (23 rendering modes)
  - Lyrics (if available)
  - Play/Pause
  - Next/Previous
  - Seek Interval (go forward/backward by x seconds, can set amount)
- Art rendering modes (press `a`):
  - 23 chafa symbol sets: ascii, braille, block, dot, border, diagonal, half, quad, sextant, technical, geometric, latin, digit, alpha, stipple, solid, inverted, narrow, wide, ambiguous, legacy, extra, all

- Help Menu for keybinds
- Refresh Button (if auto-refresh/polling does not work)
- Layout handling when resizing window

## Planned

- Configuration file

## Known Issues

Needs programme restart.
- Fallback mode check not triggered by refresh hotkey.

When opened in tmux, switching focus away and then back means programme can only
exit with `Ctrl + C` instead of `q`.

## Tested Media Players

- mpv (requires mpv-mpris)
- YouTube Web
- YouTube Music Web
- Tauon

- Firefox-based browser(s)

Should work with media players detectable by `playerctl` or those that implement
`mpris`.

> PRs welcome to add tested media player entries.

## Requirements

### Core Dependencies

Built on the shoulders of giants.

- ncursesw (TUI, wide char support)
- panelw (ncurses panel library)
- [playerctl](https://github.com/altdesktop/playerctl)
- ffmpeg (development libraries: libavformat, libavcodec, libswscale, libavutil)
- [chafa](https://hpjansson.org/chafa/) >= 1.18.2 (image-to-ASCII/braille/sixel conversion)
- glib (chafa dependency)

### Dev Dependencies

- Git
- GNU Make (build)
- meson (build)
- ninja (build)
- cmocka (unit testing)
- hyperfine (benchmarking)
- perf (benchmarking)
- strace
- awk

## Platform support

Linux.

Should work on MacOS/WSL (untested).

## Setup

Clone the repository, ensure you have listed dependencies on your system.

```bash
git clone --depth=1 https://codeberg.org/bladeacer/yams.git
```

- Use `make install` to install binary to `/usr/local/bin`.

- Use `make release` to install stripped binary to `./build/yams`.

- Use `make run` to install binary with debug to `./build/yams`.

### Binary size comparison (x86_64 Linux)

| `make` flag | Binary size | Linkage |
| --- | --- | --- |
| `make install` | ~ 36 kB | placeholder |
| `make release` | ~ 1.5 MB | static-linked |
| `make release` | ~ 600 kB | dynamically linked |

> Static-linked figures are based on an x86_64 build with musl-gcc and all
> dependencies compiled statically. Dynamically linked builds are smaller since
> libchafa, libavcodec, etc. are loaded from the system.

## LLM Usage Disclaimer

I used LLMs to help with development.

This is my first project using `ncurses`, C and UNIX tools like `strace`, `stat`.

## License

Dual licensed under either the MIT or Apache-2.0 License, at your own choice.
