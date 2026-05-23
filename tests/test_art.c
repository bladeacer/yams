#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <cmocka.h>
#include "trim.h"

static void test_trim_no_borders(void **state) {
    (void)state;
    int w = 4, h = 4;
    size_t sz = (size_t)w * h * 4;
    uint8_t *pixels = malloc(sz);
    assert_non_null(pixels);

    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int off = (y * w + x) * 4;
            pixels[off] = 0; pixels[off + 1] = 0; pixels[off + 2] = 0;
            pixels[off + 3] = 255;
        }

    int content_off = (1 * w + 1) * 4;
    pixels[content_off] = 200; pixels[content_off + 1] = 100;
    pixels[content_off + 2] = 50;

    BBox bbox = find_content_bbox(pixels, w, h, 25);
    assert_int_equal(bbox.top, 1);
    assert_int_equal(bbox.bottom, 1);
    assert_int_equal(bbox.left, 1);
    assert_int_equal(bbox.right, 1);
    free(pixels);
}

static void test_trim_black_borders(void **state) {
    (void)state;
    int w = 6, h = 6;
    size_t sz = (size_t)w * h * 4;
    uint8_t *pixels = malloc(sz);
    assert_non_null(pixels);
    memset(pixels, 0, sz);

    int cx = 1, cy = 1, cw = 4, ch = 4;
    for (int y = cy; y < cy + ch; y++)
        for (int x = cx; x < cx + cw; x++) {
            int off = (y * w + x) * 4;
            pixels[off] = 200; pixels[off + 1] = 100; pixels[off + 2] = 50;
            pixels[off + 3] = 255;
        }

    BBox bbox = find_content_bbox(pixels, w, h, 25);
    assert_int_equal(bbox.top, cy);
    assert_int_equal(bbox.bottom, cy + ch - 1);
    assert_int_equal(bbox.left, cx);
    assert_int_equal(bbox.right, cx + cw - 1);

    int out_w, out_h;
    uint8_t *cropped = crop_pixels(pixels, w, h, bbox, &out_w, &out_h);
    assert_non_null(cropped);
    assert_int_equal(out_w, cw);
    assert_int_equal(out_h, ch);
    assert_int_equal(cropped[0], 200);
    assert_int_equal(cropped[1], 100);
    assert_int_equal(cropped[2], 50);
    free(cropped);
    free(pixels);
}

static void test_trim_fuzz_rejects_near_bg(void **state) {
    (void)state;
    int w = 5, h = 5;
    size_t sz = (size_t)w * h * 4;
    uint8_t *pixels = malloc(sz);
    assert_non_null(pixels);

    for (int i = 0; i < w * h; i++) {
        pixels[i * 4] = 10; pixels[i * 4 + 1] = 12; pixels[i * 4 + 2] = 8;
        pixels[i * 4 + 3] = 255;
    }

    int off = (1 * w + 1) * 4;
    pixels[off] = 200; pixels[off + 1] = 100; pixels[off + 2] = 50;

    BBox bbox = find_content_bbox(pixels, w, h, 25);
    assert_int_equal(bbox.top, 1);
    assert_int_equal(bbox.bottom, 1);
    assert_int_equal(bbox.left, 1);
    assert_int_equal(bbox.right, 1);
    free(pixels);
}

static void test_trim_removes_border_rows_keeps_content(void **state) {
    (void)state;
    int w = 5, h = 5;
    size_t sz = (size_t)w * h * 4;
    uint8_t *pixels = malloc(sz);
    assert_non_null(pixels);

    uint8_t bg[3] = {0, 0, 0};
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int off = (y * w + x) * 4;
            if (x == 0 || x == w - 1) {
                pixels[off] = bg[0]; pixels[off + 1] = bg[1]; pixels[off + 2] = bg[2];
            } else {
                pixels[off] = 200; pixels[off + 1] = 100; pixels[off + 2] = 50;
            }
            pixels[off + 3] = 255;
        }

    BBox bbox = find_content_bbox(pixels, w, h, 25);
    assert_int_equal(bbox.top, 0);
    assert_int_equal(bbox.bottom, 4);
    assert_int_equal(bbox.left, 1);
    assert_int_equal(bbox.right, 3);

    int out_w, out_h;
    uint8_t *cropped = crop_pixels(pixels, w, h, bbox, &out_w, &out_h);
    assert_non_null(cropped);
    assert_int_equal(out_w, 3);
    assert_int_equal(out_h, 5);
    free(cropped);
    free(pixels);
}

static void test_crop_simple(void **state) {
    (void)state;
    uint8_t src[4 * 4 * 4];
    for (int i = 0; i < 4 * 4; i++) {
        src[i * 4] = (uint8_t)(i * 16);
        src[i * 4 + 1] = (uint8_t)(i * 8);
        src[i * 4 + 2] = (uint8_t)(i * 4);
        src[i * 4 + 3] = 255;
    }

    BBox bbox = {1, 2, 1, 2};
    int out_w, out_h;
    uint8_t *cropped = crop_pixels(src, 4, 4, bbox, &out_w, &out_h);
    assert_non_null(cropped);
    assert_int_equal(out_w, 2);
    assert_int_equal(out_h, 2);

    assert_int_equal(cropped[0], src[(1 * 4 + 1) * 4]);
    assert_int_equal(cropped[4], src[(1 * 4 + 2) * 4]);
    assert_int_equal(cropped[8], src[(2 * 4 + 1) * 4]);
    assert_int_equal(cropped[12], src[(2 * 4 + 2) * 4]);

    free(cropped);
}

static void test_crop_full_image(void **state) {
    (void)state;
    int w = 3, h = 3;
    size_t sz = (size_t)w * h * 4;
    uint8_t *pixels = malloc(sz);
    assert_non_null(pixels);

    for (int i = 0; i < w * h; i++) {
        pixels[i * 4] = (uint8_t)(i * 10);
        pixels[i * 4 + 1] = (uint8_t)(i * 5);
        pixels[i * 4 + 2] = (uint8_t)(i * 3);
        pixels[i * 4 + 3] = 255;
    }

    BBox bbox = {0, 2, 0, 2};
    int out_w, out_h;
    uint8_t *cropped = crop_pixels(pixels, w, h, bbox, &out_w, &out_h);
    assert_non_null(cropped);
    assert_int_equal(out_w, 3);
    assert_int_equal(out_h, 3);
    assert_int_equal(memcmp(cropped, pixels, sz), 0);
    free(cropped);
    free(pixels);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_trim_no_borders),
        cmocka_unit_test(test_trim_black_borders),
        cmocka_unit_test(test_trim_fuzz_rejects_near_bg),
        cmocka_unit_test(test_trim_removes_border_rows_keeps_content),
        cmocka_unit_test(test_crop_simple),
        cmocka_unit_test(test_crop_full_image),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
