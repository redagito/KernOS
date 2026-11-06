#pragma once
/* Byte-oriented ASCII source editor. The on-disk bytes are never padded. */
#define EDIT_CAP 16384
struct edit_buffer { char text[EDIT_CAP]; int len, pos, dirty; };
static int edit_insert(struct edit_buffer *b, char c)
{
    if (b->len >= EDIT_CAP - 1) return -1;
    for (int i = b->len; i > b->pos; --i) b->text[i] = b->text[i - 1];
    b->text[b->pos++] = c; b->text[++b->len] = 0; b->dirty = 1;
    return 0;
}
static void edit_delete(struct edit_buffer *b)
{
    if (b->pos >= b->len) return;
    for (int i = b->pos; i < b->len; ++i) b->text[i] = b->text[i + 1];
    --b->len; b->dirty = 1;
}
static int edit_home(const struct edit_buffer *b)
{
    int p = b->pos;
    while (p && b->text[p - 1] != '\n') --p;
    return p;
}
static int edit_end(const struct edit_buffer *b)
{
    int p = b->pos;
    while (p < b->len && b->text[p] != '\n') ++p;
    return p;
}
static void edit_vertical(struct edit_buffer *b, int down)
{
    int start = edit_home(b), col = b->pos - start;
    if (down) {
        int p = edit_end(b);
        if (p == b->len) return;
        start = p + 1;
    } else {
        if (!start) return;
        start -= 1;
        while (start && b->text[start - 1] != '\n') --start;
    }
    int p = start;
    while (col-- && p < b->len && b->text[p] != '\n') ++p;
    b->pos = p;
}
