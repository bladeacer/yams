# yams

Yet another media status indicator/screensaver. playerctl wrapper go brr.

Should work with media players detectable by `playerctl`.

## Tested Media Players

- Tauon
- Firefox based Browser
- mpv (requires mpv-mpris)

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

- meson
- ninja

## LLM Usage Disclaimer

I used LLMs to help with development, learning ncurses with this project.

## License

MIT.
