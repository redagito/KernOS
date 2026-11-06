#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "editor_model.h"

static struct edit_buffer doc;
static char filename[13] = "HELLO.C";
static int top_line, left_col, height, width;
static const char *message = "New file";
static char previous[64][256];

static void line(int row, const char *s)
{
    char out[256];
    int n = 0;
    while (n < width - 1 && s[n]) { out[n] = s[n]; ++n; }
    while (n < width - 1) out[n++] = ' ';
    int first = 0, last = n;
    while (first < n && previous[row][first] == out[first]) ++first;
    while (last > first && previous[row][last - 1] == out[last - 1]) --last;
    if (last > first) {
        term_move(first, row);
        term_paint(out + first, last - first);
        memcpy(previous[row], out, n);
    }
}

static void render(void)
{
    int caret_line = 0;
    for (int i = 0; i < doc.pos; ++i) if (doc.text[i] == '\n') ++caret_line;
    int caret_col = doc.pos - edit_home(&doc);
    if (caret_line < top_line) top_line = caret_line;
    if (caret_line >= top_line + height - 2) top_line = caret_line - height + 3;
    if (caret_col < left_col) left_col = caret_col;
    if (caret_col >= left_col + width - 1) left_col = caret_col - width + 2;
    char header[256];
    snprintf(header, sizeof header, "KernOS Edit | %s%s | Ctrl-S save  Ctrl-Q close  Ctrl-X discard",
             filename, doc.dirty ? " *" : "");
    line(0, header);
    int p = 0;
    for (int skip = 0; skip < top_line && p < doc.len; ++skip) {
        while (p < doc.len && doc.text[p] != '\n') ++p;
        if (p < doc.len) ++p;
    }
    for (int row = 1; row < height - 1; ++row) {
        char text[256]; int n = 0, c = 0;
        while (p < doc.len && doc.text[p] != '\n') {
            if (c++ >= left_col && n < width - 1) {
                unsigned char ch = doc.text[p];
                text[n++] = ch >= 32 && ch < 127 ? ch : '?';
            }
            ++p;
        }
        text[n] = 0;
        if (p < doc.len) ++p;
        line(row, text);
    }
    snprintf(header, sizeof header, "%s | Ln %d Col %d | %d/%d bytes | Click console to switch",
             message, caret_line + 1, caret_col + 1, doc.len, EDIT_CAP - 1);
    line(height - 1, header);
    term_move(caret_col - left_col, 1 + caret_line - top_line);
    term_caret();
}

int main(void)
{
    char args[128];
    if (process_args(args, sizeof args) > 0) {
        if (strlen(args) > 12) { puts("edit: use an 8.3 filename"); return 1; }
        strcpy(filename, args);
    }
    long face = face_geometry();
    height = (face >> 16) / 2;
    width = face & 65535;
    if (height < 4 || height > 64 || width < 20 || width > 256 || surface_claim(0, height, 1) < 0) {
        puts("edit: framebuffer surface unavailable"); return 1;
    }
    term_clear();
    long size = file_load(filename, 0, 0);
    if (size >= EDIT_CAP) { puts("edit: file exceeds 16383 bytes"); return 1; }
    if (size >= 0) {
        long n = file_load(filename, doc.text, EDIT_CAP - 1);
        if (n < 0) { puts("edit: could not load file"); return 1; }
        doc.len = n; doc.text[n] = 0; message = "Opened";
    }
    /* Serial evidence is separate from the rendered frame. */
    printf("edit: opened %s bytes=%d\n", filename, doc.len);
    for (;;) {
        render();
        unsigned char c;
        if (read(0, &c, 1) != 1) continue;
        if (c == 19) {
            long n = file_save(filename, doc.text, doc.len);
            if (n == doc.len) {
                doc.dirty = 0; message = "Saved";
                printf("edit: saved %s bytes=%d\n", filename, doc.len);
            } else { message = "SAVE FAILED - edits retained"; puts("edit: save failed"); }
        } else if (c == 17) {
            if (doc.dirty) message = "Unsaved: Ctrl-S saves; Ctrl-X discards and closes";
            else break;
        } else if (c == 24) break;
        else if (c == 0x80) { if (doc.pos) --doc.pos; }
        else if (c == 0x81) { if (doc.pos < doc.len) ++doc.pos; }
        else if (c == 0x82) edit_vertical(&doc, 0);
        else if (c == 0x83) edit_vertical(&doc, 1);
        else if (c == 0x84) doc.pos = edit_home(&doc);
        else if (c == 0x85) doc.pos = edit_end(&doc);
        else if (c == 0x86) edit_delete(&doc);
        else if (c == '\b') { if (doc.pos) { --doc.pos; edit_delete(&doc); } }
        else if (c == '\t') {
            for (int i = 0; i < 4; ++i) if (edit_insert(&doc, ' ') < 0) message = "Buffer full";
        } else if (c == '\n' || (c >= 32 && c < 127)) {
            if (edit_insert(&doc, c) < 0) message = "Buffer full";
        }
    }
    term_clear(); puts("edit: closed");
    return 0;
}
