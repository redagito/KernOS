/* ppmtext: render a QEMU screendump PPM (VGA text capture) to ASCII art.
 * Cells are 9x16 px at 720x400 (80x25 text screen). Renders full per-pixel
 * bitmaps for the requested text-cell rows: '.' blank, '#' ink.
 */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: ppmtext <ppm> <c1> [c2...]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("open"); return 2; }
    int w, h, max;
    char magic[3] = {0};
    if (fscanf(f, "%2s %d %d %d", magic, &w, &h, &max) != 4) { fprintf(stderr, "bad ppm\n"); return 1; }
    fgetc(f); /* single whitespace after max */
    unsigned char *px = malloc((size_t)w * h * 3);
    if (fread(px, 3, (size_t)w * h, f) != (size_t)w * h) { fprintf(stderr, "short ppm\n"); return 1; }
    fprintf(stderr, "ppm: %dx%d\n", w, h);

    for (int k = 2; k < argc; ++k) {
        int cell = atoi(argv[k]);
        int y0 = cell * 16;
        printf("--- text-cell row %d (px %d-%d) ---\n", cell, y0, y0 + 15);
        for (int dy = 0; dy < 16; ++dy) {
            for (int col = 0; col < 80; ++col) {
                int best = 0;
                for (int x = 0; x < 9; ++x) {
                    int px_x = col * 9 + x;
                    int y = y0 + dy;
                    if (y < h && px_x < w) {
                        const unsigned char *p = px + (y * w + px_x) * 3;
                        int b = p[0] + p[1] + p[2];
                        if (b > best) best = b;
                    }
                }
                putchar(best > 300 ? '#' : '.');
            }
            putchar('\n');
        }
    }
    return 0;
}
