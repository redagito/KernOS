#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <user/desktop.h>
#include "editor_model.h"

#define VIEW_ROWS 64
#define VIEW_COLS 256

static struct edit_buffer doc;
static char filename[13] = "HELLO.C";
static int height, width, top_line, left_col;
static int selection_anchor = -1;
static int selecting;
static char clipboard[EDIT_CAP];
static int clipboard_len;
static const char *message = "New file";
static char old_cell[VIEW_ROWS][VIEW_COLS];
static unsigned char old_selected[VIEW_ROWS][VIEW_COLS];
static int cache_valid;

static int min_i(int a, int b) { return a < b ? a : b; }
static int max_i(int a, int b) { return a > b ? a : b; }
static int valid_name(const char *s)
{
    int base=0, ext=0, dot=0;
    if (!s || !*s) return 0;
    for (; *s; ++s) {
        unsigned char c=*s;
        if (c=='.') { if (dot || !base) return 0; dot=1; continue; }
        if (c<33 || c>126 || c=='"' || c=='*' || c=='/' || c==':' ||
            c=='<' || c=='>' || c=='?' || c=='\\' || c=='|') return 0;
        if (dot) { if (++ext>3) return 0; }
        else if (++base>8) return 0;
    }
    return base && (!dot || ext);
}

static void copy_name(char dst[13], const char *src)
{
    int i = 0;
    while (src[i] && i < 12) { dst[i] = src[i]; ++i; }
    dst[i] = 0;
}

static void info_update(void)
{
    struct desktop_info info;
    int line_no = 1, line_start = 0;
    for (int i = 0; i < doc.pos; ++i) {
        if (doc.text[i] == '\n') { ++line_no; line_start = i + 1; }
    }
    snprintf(info.title, sizeof info.title, "%s%s - Notepad",
             filename[0] ? filename : "Untitled", doc.dirty ? " *" : "");
    snprintf(info.status, sizeof info.status, "%s | Ln %d Col %d | %d/%d bytes",
             message, line_no, doc.pos - line_start + 1, doc.len, EDIT_CAP - 1);
    info.dirty = doc.dirty;
    desktop(DESKTOP_INFO, &info, 0);
}

static void line_position(int pos, int *line_no, int *line_start)
{
    int row = 0, start = 0;
    if (pos > doc.len) pos = doc.len;
    for (int i = 0; i < pos; ++i) {
        if (doc.text[i] == '\n') { ++row; start = i + 1; }
    }
    *line_no = row;
    *line_start = start;
}

static int position_for(int row, int col)
{
    int p = 0, logical_line = top_line + row;
    for (int i = 0; i < logical_line && p < doc.len; ++i) {
        while (p < doc.len && doc.text[p] != '\n') ++p;
        if (p < doc.len) ++p;
    }
    while (col-- > 0 && p < doc.len && doc.text[p] != '\n') ++p;
    return p;
}

static void selection_range(int *first, int *last)
{
    if (selection_anchor < 0 || selection_anchor == doc.pos) {
        *first = *last = doc.pos;
        return;
    }
    *first = min_i(selection_anchor, doc.pos);
    *last = max_i(selection_anchor, doc.pos);
}

static int selected_at(int pos, int first, int last)
{
    return first < last && pos >= first && pos < last;
}

static void paint_run(int row, int col, const char *cells, int count, int selected)
{
    if (!count) return;
    term_move(col, row);
    term_style(selected);
    term_paint(cells, (size_t)count);
}

static void render(void)
{
    long face = term_geometry();
    int rows = face >> 16, cols = face & 65535;
    if (rows > 0 && rows <= VIEW_ROWS && cols > 0 && cols <= VIEW_COLS &&
        (height != rows || width != cols)) {
        height = rows; width = cols; cache_valid = 0;
    }
    int caret_line, caret_start;
    line_position(doc.pos, &caret_line, &caret_start);
    int caret_col = doc.pos - caret_start;
    if (caret_line < top_line) top_line = caret_line;
    if (caret_line >= top_line + height) top_line = caret_line - height + 1;
    if (caret_col < left_col) left_col = caret_col;
    if (caret_col >= left_col + width) left_col = caret_col - width + 1;
    if (top_line < 0) top_line = 0;
    if (left_col < 0) left_col = 0;

    int sel_first, sel_last;
    selection_range(&sel_first, &sel_last);
    for (int row = 0; row < height; ++row) {
        int p = position_for(row, 0);
        int logical_col = 0;
        int newline_cell = 0;
        while (logical_col < left_col && p < doc.len && doc.text[p] != '\n') {
            ++logical_col; ++p;
        }
        char cells[VIEW_COLS];
        unsigned char styles[VIEW_COLS];
        for (int col = 0; col < width; ++col) {
            int index = p;
            char ch = ' ';
            int marked = 0;
            if (p < doc.len && doc.text[p] != '\n') {
                unsigned char raw = (unsigned char)doc.text[p];
                ch = raw >= 32 && raw < 127 ? (char)raw : '?';
                marked = selected_at(index, sel_first, sel_last);
                ++p;
            } else if (p < doc.len && doc.text[p] == '\n') {
                /* Show a selected line break as a selected trailing cell. */
                marked = !newline_cell && selected_at(index, sel_first, sel_last);
                newline_cell = 1;
            }
            cells[col] = ch;
            styles[col] = (unsigned char)marked;
        }

        int col = 0;
        while (col < width) {
            if (cache_valid && old_cell[row][col] == cells[col] &&
                old_selected[row][col] == styles[col]) { ++col; continue; }
            int begin = col, style = styles[col];
            char run[VIEW_COLS]; int n = 0;
            while (col < width && n < VIEW_COLS && styles[col] == style &&
                   (!cache_valid || old_cell[row][col] != cells[col] ||
                    old_selected[row][col] != styles[col])) {
                run[n++] = cells[col++];
            }
            paint_run(row, begin, run, n, style);
        }
        for (int col2 = 0; col2 < width; ++col2) {
            old_cell[row][col2] = cells[col2];
            old_selected[row][col2] = styles[col2];
        }
    }
    cache_valid = 1;
    term_style(0);
    term_move(caret_col - left_col, caret_line - top_line);
    term_caret();
    info_update();
}

static int confirm_replace(const char *name)
{
    struct desktop_dialog dialog = {0};
    struct desktop_event event;
    dialog.kind = 3;
    snprintf(dialog.title, sizeof dialog.title, "Replace %s?", name);
    if (desktop(DESKTOP_DIALOG, &dialog, 0) < 0) return 0;
    for (;;) {
        if (desktop(DESKTOP_WAIT, &event, 0) < 0) return 0;
        if (event.type == DESKTOP_CONFIRM) return event.key == 1;
    }
}

static int save_as_name(const char *name)
{
    if (!name || !name[0]) return -1;
    if (!valid_name(name)) { message = "Use a valid 8.3 filename"; return -1; }
    if (strcmp(name, filename) && file_load(name, 0, 0) >= 0 && !confirm_replace(name)) {
        message = "Save canceled; edits retained";
        return -1;
    }
    long n = file_save(name, doc.text, (size_t)doc.len);
    if (n != doc.len) {
        message = "Save failed; edits retained";
        puts("edit: save failed");
        return -1;
    }
    copy_name(filename, name);
    doc.dirty = 0;
    message = "Saved";
    printf("edit: saved %s bytes=%d\n", filename, doc.len);
    return 0;
}

static int save_current(void)
{
    if (!filename[0]) return -1;
    return save_as_name(filename);
}

static int filename_dialog(const char *title, const char *initial, char out[16])
{
    struct desktop_dialog dialog;
    struct desktop_event event;
    dialog.kind = 1;
    snprintf(dialog.title, sizeof dialog.title, "%s", title);
    copy_name(dialog.text, initial);
    if (desktop(DESKTOP_DIALOG, &dialog, 0) < 0) return -1;
    for (;;) {
        if (desktop(DESKTOP_WAIT, &event, 0) < 0) return -1;
        if (event.type == DESKTOP_FILE) {
            copy_name(out, event.name);
            return out[0] ? 0 : -1;
        }
    }
}

static int confirm_dirty(void)
{
    struct desktop_dialog dialog;
    struct desktop_event event;
    if (!doc.dirty) return 2; /* discard/no-save is safe when clean */
    dialog.kind = 2;
    snprintf(dialog.title, sizeof dialog.title, "Save changes?");
    copy_name(dialog.text, filename);
    if (desktop(DESKTOP_DIALOG, &dialog, 0) < 0) return 3;
    for (;;) {
        if (desktop(DESKTOP_WAIT, &event, 0) < 0) return 3;
        if (event.type == DESKTOP_CONFIRM) return event.key;
    }
}

enum pending_action { ACTION_NONE, ACTION_NEW, ACTION_OPEN, ACTION_CLOSE };
static enum pending_action pending;
static char pending_name[16];

static void perform_pending(void)
{
    if (pending == ACTION_NEW) {
        memset(&doc, 0, sizeof doc);
        filename[0] = 0;
        selection_anchor = -1; selecting = 0;
        top_line = left_col = 0; cache_valid = 0;
        message = "New file";
    } else if (pending == ACTION_OPEN) {
        static char loaded[EDIT_CAP];
        long size = file_load(pending_name, 0, 0);
        if (size < 0) {
            memset(&doc, 0, sizeof doc);
            copy_name(filename, pending_name);
            selection_anchor = -1; selecting = 0;
            top_line = left_col = 0; cache_valid = 0; message = "New file";
            printf("edit: opened %s bytes=0\n", filename);
        } else if (size >= EDIT_CAP) {
            message = size >= EDIT_CAP ? "File exceeds 16383 bytes" : "Open failed";
            puts("edit: open failed");
        } else {
            long got = file_load(pending_name, loaded, EDIT_CAP);
            if (got < 0 || got != size) {
                message = "Open failed; edits retained";
                puts("edit: open failed");
            } else {
                for (int i = 0; i < got; ++i) {
                    unsigned char c = loaded[i];
                    if ((c < 32 && c != '\n' && c != '\r' && c != '\t') || c >= 127) {
                        message = "Open refused: ASCII text files only";
                        pending = ACTION_NONE;
                        return;
                    }
                }
                memcpy(doc.text, loaded, (size_t)got);
                doc.text[got] = 0; doc.len = (int)got; doc.pos = 0; doc.dirty = 0;
                copy_name(filename, pending_name);
                selection_anchor = -1; selecting = 0;
                top_line = left_col = 0; cache_valid = 0;
                message = "Opened";
                printf("edit: opened %s bytes=%d\n", filename, doc.len);
            }
        }
    } else if (pending == ACTION_CLOSE) {
        desktop(DESKTOP_HIDE, 0, 0);
        puts("edit: closed (window hidden)");
    }
    pending = ACTION_NONE;
}

static void request_action(enum pending_action action, const char *name)
{
    if (action == ACTION_OPEN && !valid_name(name)) {
        message = "Use a valid 8.3 filename";
        return;
    }
    pending = action;
    pending_name[0] = 0;
    if (name) copy_name(pending_name, name);
    int answer = confirm_dirty();
    if (answer == 1) {
        if (!filename[0]) {
            char name[16] = {0};
            if (filename_dialog("Save as", "", name) < 0 || save_as_name(name) < 0) {
                pending = ACTION_NONE;
                return;
            }
        } else if (save_current() < 0) {
            pending = ACTION_NONE;
            return;
        }
        perform_pending();
    } else if (answer == 2) {
        if (action == ACTION_CLOSE && doc.dirty) {
            long n = file_load(filename, doc.text, EDIT_CAP - 1);
            if (n < 0) n = 0;
            doc.len = n; doc.text[n] = 0; doc.pos = 0; doc.dirty = 0;
            top_line = left_col = 0; selection_anchor = -1; cache_valid = 0;
            message = "Changes discarded";
        }
        perform_pending();
    } else {
        pending = ACTION_NONE;
        message = "Canceled";
    }
}

static void request_open_dialog(void)
{
    char name[16] = {0};
    if (filename_dialog("Open file", filename, name) < 0) return;
    request_action(ACTION_OPEN, name);
}

static void request_save_as(void)
{
    char name[16] = {0};
    if (filename_dialog("Save as", filename, name) < 0) return;
    save_as_name(name);
}

static void delete_selection(void)
{
    int first, last;
    selection_range(&first, &last);
    if (first == last) return;
    for (int i = last; i <= doc.len; ++i) doc.text[first + i - last] = doc.text[i];
    doc.len -= last - first; doc.dirty = 1;
    doc.pos = first;
    selection_anchor = -1; selecting = 0;
}

static void copy_selection(int cut)
{
    int first, last;
    selection_range(&first, &last);
    if (first == last) return;
    clipboard_len = last - first;
    memcpy(clipboard, doc.text + first, (size_t)clipboard_len);
    if (cut) delete_selection();
    message = cut ? "Cut" : "Copied";
}

static void paste_clipboard(void)
{
    if (!clipboard_len) { message = "Clipboard empty"; return; }
    int first, last;
    selection_range(&first, &last);
    if (doc.len - (last - first) + clipboard_len >= EDIT_CAP) {
        message = "Paste exceeds buffer; edits retained";
        return;
    }
    delete_selection();
    for (int i = 0; i < clipboard_len; ++i) {
        if (edit_insert(&doc, clipboard[i]) < 0) {
            message = "Buffer full";
            return;
        }
    }
    selection_anchor = -1; selecting = 0;
    message = "Pasted";
}

static void key_event(int key)
{
    if (key == 1) { /* Ctrl-A */
        selection_anchor = 0; doc.pos = doc.len; selecting = 0;
    } else if (key == 3) copy_selection(0);       /* Ctrl-C */
    else if (key == 24) copy_selection(1);        /* Ctrl-X */
    else if (key == 22) paste_clipboard();        /* Ctrl-V */
    else if (key == 14) request_action(ACTION_NEW, 0); /* Ctrl-N */
    else if (key == 15) request_open_dialog();    /* Ctrl-O */
    else if (key == 19) {
        if (save_current() < 0) request_save_as();
    } else if (key == 17) request_action(ACTION_CLOSE, 0); /* Ctrl-Q */
    else if (key == 0x80) {
        if (doc.pos) --doc.pos;
        selection_anchor = -1; selecting = 0;
    } else if (key == 0x81) {
        if (doc.pos < doc.len) ++doc.pos;
        selection_anchor = -1; selecting = 0;
    } else if (key == 0x82 || key == 0x83) {
        edit_vertical(&doc, key == 0x83);
        selection_anchor = -1; selecting = 0;
    } else if (key == 0x84) {
        doc.pos = edit_home(&doc); selection_anchor = -1; selecting = 0;
    } else if (key == 0x85) {
        doc.pos = edit_end(&doc); selection_anchor = -1; selecting = 0;
    } else if (key == 0x86) {
        if (selection_anchor >= 0 && selection_anchor != doc.pos) delete_selection();
        else { edit_delete(&doc); selection_anchor = -1; selecting = 0; }
    } else if (key == '\b') {
        if (selection_anchor >= 0 && selection_anchor != doc.pos) delete_selection();
        else if (doc.pos) { --doc.pos; edit_delete(&doc); selection_anchor = -1; selecting = 0; }
    } else if (key == '\t') {
        delete_selection();
        for (int i = 0; i < 4; ++i) if (edit_insert(&doc, ' ') < 0) message = "Buffer full";
        selection_anchor = -1; selecting = 0;
    } else if (key == '\n' || (key >= 32 && key < 127)) {
        delete_selection();
        if (edit_insert(&doc, (char)key) < 0) message = "Buffer full";
        selection_anchor = -1; selecting = 0;
    }
}

static void click_event(int x, int y, int drag)
{
    if (x < 0) x = 0;
    if (x >= width) x = width - 1;
    if (y < 0) y = 0;
    if (y >= height) y = height - 1;
    int pos = position_for(y, x + left_col);
    if (!drag) selection_anchor = pos;
    else if (!selecting && selection_anchor < 0) selection_anchor = pos;
    doc.pos = pos;
    selecting = drag;
}

static void command_event(int command)
{
    switch (command) {
    case DESKTOP_NEW: request_action(ACTION_NEW, 0); break;
    case DESKTOP_LOAD: request_open_dialog(); break;
    case DESKTOP_SAVE:
        if (save_current() < 0) request_save_as();
        break;
    case DESKTOP_SAVE_AS: request_save_as(); break;
    case DESKTOP_CLOSE: request_action(ACTION_CLOSE, 0); break;
    case DESKTOP_SELECT_ALL: selection_anchor = 0; doc.pos = doc.len; selecting = 0; break;
    case DESKTOP_CUT: copy_selection(1); break;
    case DESKTOP_COPY: copy_selection(0); break;
    case DESKTOP_PASTE: paste_clipboard(); break;
    }
}

static void open_external(const char *name)
{
    if (!name || !name[0]) return;
    request_action(ACTION_OPEN, name);
}

int main(void)
{
    char args[128];
    if (process_args(args, sizeof args) > 0) {
        if (strlen(args) > 12) { puts("edit: use an 8.3 filename"); return 1; }
        copy_name(filename, args);
    }
    if (desktop(DESKTOP_ATTACH, 0, DESKTOP_EDITOR) < 0) {
        puts("edit: desktop surface unavailable");
        return 1;
    }
    long geometry = term_geometry();
    height = (int)(geometry >> 16);
    width = (int)(geometry & 65535);
    if (height < 1 || height > VIEW_ROWS || width < 1 || width > VIEW_COLS) {
        puts("edit: desktop surface unavailable");
        return 1;
    }
    memset(&doc, 0, sizeof doc);
    long size = file_load(filename, 0, 0);
    if (size >= EDIT_CAP) {
        filename[0] = 0;
        message = "File exceeds 16383 bytes; open another file";
    } else if (size >= 0) {
        long n = file_load(filename, doc.text, EDIT_CAP);
        if (n < 0 || n != size) {
            doc.text[0] = 0; filename[0] = 0;
            message = "Could not load file; open another file";
        } else {
            int good=1;
            for (int i=0;i<n;++i) {
                unsigned char c=doc.text[i];
                if ((c<32 && c!='\n' && c!='\r' && c!='\t') || c>=127) good=0;
            }
            if (good) { doc.len = (int)n; doc.text[n] = 0; message = "Opened"; }
            else { doc.text[0]=0; filename[0]=0; message="ASCII text files only; open another file"; }
        }
    } else {
        message = "New file";
    }
    printf("edit: opened %s bytes=%d\n", filename, doc.len);

    for (;;) {
        render();
        struct desktop_event event;
        if (desktop(DESKTOP_WAIT, &event, 0) < 0) continue;
        switch (event.type) {
        case DESKTOP_KEY: key_event(event.key); break;
        case DESKTOP_COMMAND: command_event(event.key); break;
        case DESKTOP_CLICK: click_event(event.x, event.y, 0); break;
        case DESKTOP_DRAG: click_event(event.x, event.y, 1); break;
        case DESKTOP_FILE: open_external(event.name); break;
        case DESKTOP_RESIZE: {
            geometry = term_geometry();
            int rows = (int)(geometry >> 16), cols = (int)(geometry & 65535);
            if (rows > 0 && rows <= VIEW_ROWS && cols > 0 && cols <= VIEW_COLS) {
                height = rows; width = cols; cache_valid = 0;
                top_line = left_col = 0;
            }
            break;
        }
        case DESKTOP_CONFIRM:
            /* Modal answers are consumed by their synchronous helper. */
            break;
        }
    }
}
