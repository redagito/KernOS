#ifndef NDEBUG
#define assert(x) ((x)?(void)0:__kcc_assert(#x,__FILE__,__LINE__))
void __kcc_assert(const char*,const char*,int);
#else
#define assert(x) ((void)0)
#endif
