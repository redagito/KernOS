#ifndef KCC_SYS_TIME_H
#define KCC_SYS_TIME_H
struct timeval { long tv_sec, tv_usec; }; int gettimeofday(struct timeval*,void*);
#endif
