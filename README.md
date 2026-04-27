# yams

Yet another media status indicator/screensaver.

`playerctl` wrapper go brr.

## Features

- CJK/Unicode Support (`ncursesw`)
- Currently Playing Media
  - Metadata (Media name/title/etc.)
  - ASCII art (extracted from audio file/URL)
  - Lyrics (if available)
- Refresh Button (if auto-refresh/polling does not work)

## Known Issues

- TUI does not dynamically resize when window size is changed at runtime.
Refresh button does not help for this.

## Tested Media Players

- mpv (requires mpv-mpris)
<!-- - YouTube Web -->
- YouTube Music Web
- Tauon

- Firefox-based browser(s)

Should work with media players detectable by `playerctl` via `mpris`.

> PRs welcome to add tested media player entries.

## Requirements

### Core Dependencies

- ncurses (TUI)
- playerctl (Get media playing status)
- ffmpeg (Extract image thumbnail from audio file)
- imagemagick (Convert to WEBP, used so jp2a handles image scaling better)
- jp2a (JPG/WEBP to ASCII)
- sed (Regex replace)
- timeout (Time limit image rendering)

### Dev Dependencies

- GNU Make
- meson
- ninja

## Platform support

Linux.

## Setup

Clone the repository, ensure you have listed dependencies on your system.

Use `make install` to install binary to `/usr/local/bin`.

Use `make release` to install stripped binary to `./build/yams`.

Use `make run` to install binary with debug to `./build/yams`.

### Binary size comparison

| `make` flag | Binary size |
| --- | --- |
| `make install` | ~ 23.2 kB |
| `make release` | ~ 29.1 kB |
| `make run` | ~ 56.5 kB |

## LLM Usage Disclaimer

I used LLMs to help with development.

This is my first project using `ncurses`, C and UNIX tools like `strace`, `stat`.

## License

Apache-2.0.
