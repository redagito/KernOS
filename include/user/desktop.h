#pragma once
/* Fixed-size desktop ABI. All calls are serialized by the syscall gate. */
#define DESKTOP_ATTACH 0
#define DESKTOP_WAIT 1
#define DESKTOP_INFO 2
#define DESKTOP_HIDE 3
#define DESKTOP_OPEN 4
#define DESKTOP_DIALOG 5
#define DESKTOP_EDITOR 1
#define DESKTOP_CONSOLE 2
#define DESKTOP_KEY 1
#define DESKTOP_COMMAND 2
#define DESKTOP_CLICK 3
#define DESKTOP_DRAG 4
#define DESKTOP_FILE 5
#define DESKTOP_CONFIRM 6
#define DESKTOP_RESIZE 7
#define DESKTOP_NEW 1
#define DESKTOP_LOAD 2
#define DESKTOP_SAVE 3
#define DESKTOP_SAVE_AS 4
#define DESKTOP_CLOSE 5
#define DESKTOP_SELECT_ALL 6
#define DESKTOP_CUT 7
#define DESKTOP_COPY 8
#define DESKTOP_PASTE 9
struct desktop_event {
    int type, key, x, y;
    char name[16];
};
struct desktop_info {
    char title[40], status[96];
    int dirty;
};
/* kind 1: filename prompt; kind 2: save changes; kind 3: replace file.
   CONFIRM.key = 1 Save, 2 Discard, 3 Cancel. FILE.name = accepted name,
   or empty on Cancel. Dialogs are modal to their owning window. */
struct desktop_dialog {
    int kind;
    char title[40], text[16];
};
long desktop(long operation, void *data, long argument);
