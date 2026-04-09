/*
 * compositor.c — AMOS screen compositor
 *
 * Composites multiple AMOS screens into a single output framebuffer.
 * Screens are layered by priority (0 = front), with transparency.
 */

#include "amos.h"
#include <string.h>

void compositor_render(amos_state_t *state, uint32_t *output, int out_w, int out_h)
{
    /* Clear output to black */
    memset(output, 0, out_w * out_h * sizeof(uint32_t));

    /* Sort screens by priority (back to front) — simple insertion sort */
    int order[AMOS_MAX_SCREENS];
    int active_count = 0;
    for (int i = 0; i < AMOS_MAX_SCREENS; i++) {
        if (state->screens[i].active && state->screens[i].visible) {
            order[active_count++] = i;
        }
    }

    /* Sort by priority descending (higher priority = drawn later = on top) */
    for (int i = 1; i < active_count; i++) {
        int key = order[i];
        int j = i - 1;
        while (j >= 0 && state->screens[order[j]].priority < state->screens[key].priority) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }

    /* Composite each screen */
    for (int s = 0; s < active_count; s++) {
        amos_screen_t *scr = &state->screens[order[s]];

        /* Calculate source and destination rectangles */
        int src_x = scr->offset_x;
        int src_y = scr->offset_y;
        int dst_x = scr->display_x;
        int dst_y = scr->display_y;
        int copy_w = scr->display_w > 0 ? scr->display_w : scr->width;
        int copy_h = scr->display_h > 0 ? scr->display_h : scr->height;

        /* Scale factor: in classic mode, map screen pixels to output pixels */
        /* For now, 1:1 copy (will add scaling later) */
        for (int y = 0; y < copy_h; y++) {
            int sy = src_y + y;
            int dy = dst_y + y;
            if (sy < 0 || sy >= scr->height || dy < 0 || dy >= out_h) continue;

            for (int x = 0; x < copy_w; x++) {
                int sx = src_x + x;
                int dx = dst_x + x;
                if (sx < 0 || sx >= scr->width || dx < 0 || dx >= out_w) continue;

                uint32_t pixel = scr->pixels[sy * scr->width + sx];
                uint8_t alpha = pixel & 0xFF;

                if (alpha > 0) {
                    /* Color 0 is transparent in multi-screen compositing */
                    if (pixel != scr->palette[0] || s == 0) {
                        output[dy * out_w + dx] = pixel;
                    }
                }
            }
        }
    }
}

/* ── Drawing primitives not yet implemented ──────────────────────── */

void amos_screen_display(amos_state_t *state, int id, int x, int y, int w, int h)
{
    if (id < 0 || id >= AMOS_MAX_SCREENS) return;
    amos_screen_t *scr = &state->screens[id];
    if (!scr->active) return;

    scr->display_x = x;
    scr->display_y = y;
    if (w > 0) scr->display_w = w;
    if (h > 0) scr->display_h = h;
}
