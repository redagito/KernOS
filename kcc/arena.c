/* Bounded first-fit heap for the CPL3 compiler process. */
#include <stddef.h>
#include <stdint.h>

#define ARENA_BYTES (1024u * 1024u)
typedef struct __attribute__((aligned(16))) Block { size_t size; struct Block *next; int free; } Block;
static union { long double align; unsigned char bytes[ARENA_BYTES]; } arena;
static Block *head;
static size_t align_up(size_t n) { return (n + 15u) & ~(size_t)15u; }

void *malloc(size_t n)
{
    Block *b;
    if (!n) n = 1;
    if (n > ARENA_BYTES - sizeof(Block)) return 0;
    n = align_up(n);
    if (n > ARENA_BYTES - sizeof(Block)) return 0;
    if (!head) { head = (Block*)arena.bytes; head->size = ARENA_BYTES-sizeof(Block); head->next=0; head->free=1; }
    for (b=head; b; b=b->next) if (b->free && b->size >= n) {
        if (b->size >= n + sizeof(Block) + sizeof(void*)) {
            Block *tail=(Block*)((unsigned char*)(b+1)+n);
            tail->size=b->size-n-sizeof(Block); tail->next=b->next; tail->free=1;
            b->next=tail; b->size=n;
        }
        b->free=0; return b+1;
    }
    return 0;
}
void free(void *p)
{
    Block *b;
    if (!p || (unsigned char*)p < arena.bytes || (unsigned char*)p >= arena.bytes+ARENA_BYTES) return;
    b=((Block*)p)-1; b->free=1;
    for (b=head; b && b->next;) {
        if (b->free && b->next->free) {
            b->size += sizeof(Block)+b->next->size;
            b->next=b->next->next;
        } else b = b->next;
    }
}
void *calloc(size_t n,size_t z) { size_t i,total; unsigned char *p;if(z&&n>(size_t)-1/z)return 0;total=n*z;p=malloc(total);if(p)for(i=0;i<total;i++)p[i]=0;return p; }
void *realloc(void *p,size_t n) { Block *b; void *q; size_t i,old; if(!p)return malloc(n); if(!n){free(p);return 0;} b=((Block*)p)-1; old=b->size; if(old>=n)return p; q=malloc(n); if(!q)return 0; for(i=0;i<old;i++)((unsigned char*)q)[i]=((unsigned char*)p)[i]; free(p); return q; }
