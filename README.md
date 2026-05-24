# yams

![Screenshot](preview.webp)

Yet another media screensaver.
> Or status indicator

`playerctl` wrapper go brr.

## Features

- CJK/Unicode Support (`ncursesw`)
- Currently Playing Media (`playerctl`)
  - Metadata (title, artist, album, composer, genre, length, path, lyrics)
  - Album art rendered as ASCII/braille/sixel (24 rendering modes)
  - Lyrics (if available via MPRIS `xesam:asText`)
  - Play/Pause, Next/Previous, Seek forward/backward
- Art rendering modes (press `a`):
  - 23 chafa symbol sets: ascii, braille, block, dot, border, diagonal, half, quad, sextant, technical, geometric, latin, digit, alpha, stipple, solid, inverted, narrow, wide, ambiguous, legacy, extra, all
  - 1 pixel mode: sixel (requires sixel-capable terminal)
- Remote album art caching — downloaded art is stored locally under `~/.cache/yams/art/` for reuse without re-downloading
- Auto-retry for failed art downloads — retries at 1s/2s/5s/10s/30s intervals with visual feedback
- Cache management — clear art cache (`c`) and view cache size in the help bar
- Debounced art re-rendering (configurable interval, default 200ms)
- Player health monitoring — automatic fallback when the active player disappears, using event-driven `waitpid` detection with 5s stale-data polling fallback
- Player selector (`p`) — switch between multiple playerctl players at runtime
- Refresh (`r`) — force immediate art retry and re-render
- Help overlay (`?`) for all keybinds
- Automatic layout handling on terminal resize
- Configurable seek interval
- Status messages (clear cache, no players, etc.) auto-clear after 3 seconds

## Planned

- Configuration file

## Known Issues

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
- cmocka (unit testing; meson falls back to a bundled subproject if not found)
<!-- - hyperfine (benchmarking) -->
<!-- - perf (benchmarking) -->
<!-- - strace -->
- awk

## Platform support

Linux.

Should work on MacOS/WSL (untested).

## Setup

Clone the repository, ensure you have listed dependencies on your system.

```bash
git clone --depth=1 https://codeberg.org/bladeacer/yams.git
```

- Use `make install` to build a static binary and install to `/usr/local/bin`.
  Requires static libraries of all dependencies (see below).

- Use `make install-dynamic` to build a dynamically linked binary and install.

- Use `make release` to build a stripped static binary in `./build/yams`.

- Use `make release-dynamic` to build a stripped dynamically linked binary.

- Use `make run` to build a debug binary in `./build/yams`.

### Installing cmocka (optional, for running tests)

cmocka is used for unit tests. If not installed, meson will automatically
download and build it as a subproject — no manual setup required.

To install cmocka system-wide:

**Arch Linux:**
```bash
sudo pacman -S cmocka
```

**Debian/Ubuntu:**
```bash
sudo apt install libcmocka-dev
```

**Fedora:**
```bash
sudo dnf install cmocka-devel
```

**macOS (Homebrew):**
```bash
brew install cmocka
```

**From source:**
```bash
git clone https://git.cryptomilk.org/projects/cmocka.git
cd cmocka
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release
make
sudo make install
```

### Binary size (x86_64 Linux)

| Command | Binary size | Linkage | Notes |
| --- | --- | --- | --- |
| `make run` | ~ 300 kB | dynamic | Debug build, unstripped |
| `make release` | ~ 1.5 MB | static | Release, stripped, in `./build/yams` |
| `make release-dynamic` | ~ 250 kB | dynamic | Release, stripped |
| `make install` | ~ 1.5 MB | static | Release, stripped, installed |
| `make install-dynamic` | ~ 250 kB | dynamic | Release, stripped, installed |

Static builds (`make release`, `make install`) link all dependencies (libchafa,
ffmpeg, glib, ncursesw, panelw) directly into the binary. Static library
versions (`.a` files) must be installed on your system:

- Debian/Ubuntu: `apt install libchafa-dev libavformat-dev libavcodec-dev libswscale-dev libavutil-dev libncurses-dev libglib2.0-dev` (some may need `-static` variants)
- Arch: `pacman -S chafa ffmpeg ncurses glib2` (static libs included in dev packages)
- Fedora: `dnf install chafa-devel ffmpeg-devel ncurses-devel glib2-devel`

## LLM Usage Disclaimer

I used LLMs to help with development.

This is my first project using `ncurses`, C and UNIX tools like `strace`, `stat`,
as well as the others used in the project.

## License

Dual licensed under either the MIT or Apache-2.0 License, at your own choice.
