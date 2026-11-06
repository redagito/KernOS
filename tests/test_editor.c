#include <stdio.h>
#include <string.h>
#include "user/editor_model.h"
static int failed, checks;
static void check(int good, const char *what)
{ ++checks; if (!good) ++failed; printf("%s %s\n", good ? "PASS" : "FAIL", what); }
int main(void)
{
    static struct edit_buffer b;
    const char *text = "abc\nx\nlast";
    for (int i = 0; text[i]; ++i) edit_insert(&b, text[i]);
    check(!strcmp(b.text, text) && b.len == 10, "multiline insertion preserves bytes and terminator");
    b.pos = 2; edit_insert(&b, 'Z');
    check(!strcmp(b.text, "abZc\nx\nlast"), "insertion moves the suffix");
    --b.pos; edit_delete(&b);
    check(!strcmp(b.text, text), "deletion repairs the suffix and terminator");
    b.pos = 2; edit_vertical(&b, 1);
    check(b.pos == 5, "down clamps column on a shorter line");
    edit_vertical(&b, 1);
    check(b.pos == 7, "down preserves available column");
    edit_vertical(&b, 0);
    check(b.pos == 5 && edit_home(&b) == 4 && edit_end(&b) == 5, "up/home/end respect line boundaries");
    b.pos = b.len; edit_delete(&b);
    check(b.len == 10, "delete at end is inert");
    b.pos = 0; edit_vertical(&b, 0);
    check(b.pos == 0, "up on first line is inert");
    b.len = b.pos = EDIT_CAP - 1; b.text[b.len] = 0;
    check(edit_insert(&b, 'x') == -1 && b.len == EDIT_CAP - 1 && !b.text[b.len], "capacity refusal keeps buffer valid");
    printf("%d checks, %d failures\n", checks, failed);
    return failed != 0;
}
