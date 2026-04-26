# yams

Yet another media status indicator/screensaver. playerctl wrapper go brr.

Should work with media players detectable by `playerctl`.

## Tested Media Players

- Tauon

## Requirements

### Core Dependencies

- ncurses (TUI)
- playerctl (Get media playing status)
- ffmpeg (Extract image thumbnail from audio file)
- imagemagick (Convert to WEBP, used so jp2a handles image scaling better)
- jp2a (JPG/WEBP to ASCII)

### Dev Dependencies

- meson
- ninja

## License

MIT.
