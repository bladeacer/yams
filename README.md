# yams
![Screenshot](preview.webp)

yams (yet another media screensaver) is a media screensaver that displays
MPRIS metadata and album art (ASCII/braille/sixel/others) in the terminal.

## Features

### Interface

- Album art (`a`): ASCII/braille/sixel rendering (requires sixel-capable terminal).
Press `a` to configure available options.

- Help (`?`): help menu with keyboard shortcut mappings shown.

- Refresh (`r`): force immediate art retry and re-render

- Unicode/CJK support: Unicode/CJK support (via `ncurses`,
depends on active system fonts).

- Fallback/Adaptive UI: Automatic layout handling on terminal resize, fallback UI
 when no players are detected.

- Status feedback, fallback text where necessary.

### Media

- Get metadata from MPRIS-compliant players (via `playerctl`)
  - Includes title, artist, album, composer, genre, length, path, art, lyrics

- Media Playback controls:
  - Play/Pause (`Spacebar`)
  - Previous/Next(`h/l`)
  - Seek forward/backward (`j/k`, configure interval with `+/-` or `s`)

- Remote album art caching: Cached art is stored locally under `~/.cache/yams/art/`

- Auto-retry failed downloads: Auto-retries remote art downloads at increasing intervals

- Cache management: Clear art cache (`c`), view cache size

- Player selector (`p`): switch between multiple playerctl players at runtime;
metadata/art/lyrics update immediately

## Planned

- Configuration file
- Being more flexible/modular on directory conventions for platforms like MacOS.

## Known Issues

When opened in tmux, switching focus away and then back means programme can only
exit with `Ctrl + C` instead of `q`.

## Tested Media Players

- mpv (requires `mpv-mpris`)
- YouTube Web
- YouTube Music Web
- Tauon

- Firefox-based browser(s)

Should work with media players detectable by `playerctl` or those that implement
`mpris`.

> PRs welcome to add tested media player entries.

## Quick Start

```bash
git clone --depth=1 https://codeberg.org/bladeacer/yams.git
cd yams

# Install system-wide
make install
yams

# Run with debug flags
make run

# Run tests
make test
```

## Nix

A `flake.nix` is provided for a development shell with all dependencies:

```bash
nix develop
make install
yams
```

## Dependencies

### Core

- [ncursesw](https://invisible-island.net/ncurses/): TUI with wide char support
- [panelw](https://invisible-island.net/ncurses/): ncurses panel library
- [playerctl](https://github.com/altdesktop/playerctl): MPRIS D-Bus controller
- [ffmpeg](https://ffmpeg.org/): libavformat/libavcodec/libswscale/libavutil
(image decoding)
- [chafa](https://hpjansson.org/chafa/) >= 1.18.2: Image to ASCII/braille/other
charsets/sixel conversion
- glib: chafa dependency

### Build

- Git
- GNU Make
- [meson](https://mesonbuild.com/)
- [ninja](https://ninja-build.org/)
- awk

### Test

- [cmocka](https://cmocka.org/): unit testing (meson falls back to a bundled
subproject if not found)

---

### Linux

#### Debian/Ubuntu based distros
```bash
sudo apt install \
  libncursesw6 libncurses-dev \
  libpanelw6 libpanel-dev \
  playerctl \
  libavformat-dev libavcodec-dev libswscale-dev libavutil-dev \
  chafa libchafa-dev \
  libglib2.0-dev \
  meson ninja-build cmocka

git clone --depth=1 https://codeberg.org/bladeacer/yams.git
cd yams
make install
yams
```

#### Arch based distros
```bash
sudo pacman -S \
  ncurses \
  playerctl \
  ffmpeg \
  chafa \
  glib2 \
  meson ninja cmocka

git clone --depth=1 https://codeberg.org/bladeacer/yams.git
cd yams
make install
yams
```

#### Fedora based distros
```bash
sudo dnf install \
  ncurses-devel \
  playerctl \
  ffmpeg-devel \
  chafa-devel \
  glib2-devel \
  meson ninja-build cmocka-devel

git clone --depth=1 https://codeberg.org/bladeacer/yams.git
cd yams
make install
yams
```

### macOS

Requires [Homebrew](https://brew.sh/):

```bash
brew install \
  ncurses \
  playerctl \
  ffmpeg \
  chafa \
  glib \
  meson ninja cmocka

git clone --depth=1 https://codeberg.org/bladeacer/yams.git
cd yams
make install
yams
```

> **Note:** macOS support is community-tested: terminal features
> (sixel, braille art) may vary.

## Build targets

| Command | Binary | Linkage | Use case |
| --- | --- | --- | --- |
| `make run` | `./build/yams` | dynamic | Development, run in place |
| `make release` | `./build/yams` | static | Stripped, portable binary |
| `make release-dynamic` | `./build/yams` | dynamic | Stripped, dynamic |
| `make install` | `/usr/local/bin/yams` | static | Stripped, System-wide install |
| `make install-dynamic` | `/usr/local/bin/yams` | dynamic | Stripped, System-wide install |
| `make test` | N/A | dynamic | Build + run unit tests |

Static builds link all dependencies (chafa, ffmpeg, glib, ncursesw, panelw)
directly into the binary. Static library variants (`.a` files) must be installed.

These should be there if the installation instructions are followed.

### Running tests

```bash
make test
# or
ninja -C build test
```

cmocka is optional: meson will automatically fetch and build it as a
subproject if not found system-wide.

## LLM Usage Disclaimer

I used LLMs to help with development.

This is my first project using `ncurses`, C and UNIX tools like `strace`, `stat`,
as well as the others used in the project.

## License

Dual licensed under either the MIT or Apache-2.0 License, at your own choice.
