#ifndef ART_H
#define ART_H

/**
 * Renders the album art using the ffmpeg -> magick -> jp2a/chafa pipeline.
 * x, y: Top-left corner coordinates
 * w, h: Target width and height in characters
 */
pid_t render_art(int x, int y, int w, int h);

#endif
