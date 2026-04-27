#ifndef ART_H
#define ART_H

/**
 * Renders the album art using the ffmpeg -> magick -> jp2a pipeline.
 * x, y: Top-left corner coordinates
 * w, h: Target width and height in characters
 */
void render_art(int x, int y, int w, int h);

#endif
