/* File, console and small libc compatibility for TinyCC on KernOS. */
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#undef write
extern ssize_t write(int,const void*,size_t);

extern ssize_t file_load(const char*,void*,size_t);
extern ssize_t file_save(const char*,const void*,size_t);
extern long process_args(char*,size_t);
static int format(char*,size_t,const char*,va_list);
int errno;

#define FILE_CAP (512u*1024u)
#define FD_COUNT 8
#define FD_BASE 3
typedef struct KccFile { int used, mode, dirty, eof; long pos; size_t len,cap; char name[32]; unsigned char *data; } KccFile;
static KccFile files[FD_COUNT];
static KccFile out_file={1,1,0,0,0,0,0,"",0};
static KccFile err_file={1,1,0,0,0,0,0,"",0};
static KccFile in_file={1,0,0,0,0,0,0,"",0};
FILE *stdout=(FILE*)&out_file, *stderr=(FILE*)&err_file, *stdin=(FILE*)&in_file;
static void copystr(char *d,const char*s,size_t n){size_t i=0;while(i+1<n&&s[i]){d[i]=s[i];i++;}d[i]=0;}
static int short_name(const char *in,char *out,size_t cap){const char*p=in;if(!p||!*p)return 0;while(*p=='/')p++;if(!strncmp(p,"INCLUDE/",8))p+=8;if(!*p||strchr(p,'/')||strlen(p)>=cap)return 0;copystr(out,p,cap);return 1;}
static KccFile *fdfile(int fd){fd-=FD_BASE;if(fd<0||fd>=FD_COUNT||!files[fd].used)return 0;return &files[fd];}
int kcc_open(const char *name,int flags,...)
{
    int i; KccFile *f=0; ssize_t n; size_t want; char canonical[32];
    if(!short_name(name,canonical,sizeof canonical)){errno=EINVAL;return -1;}
    if(!f)for(i=0;i<FD_COUNT;i++)if(!files[i].used){f=&files[i];break;}
    if(!f){errno=ENOMEM;return -1;}
    if(!f->used){f->used=1;f->dirty=0;copystr(f->name,canonical,sizeof f->name);n=file_load(canonical,0,0);if(n<0&&!(flags&O_CREAT)){f->used=0;errno=ENOENT;return -1;}f->len=n<0?0:(size_t)n;want=(flags&O_TRUNC)?0:f->len;f->cap=want?want:4096;if(f->cap>FILE_CAP){f->used=0;errno=ENOMEM;return -1;}f->data=malloc(f->cap);if(!f->data){f->used=0;errno=ENOMEM;return -1;}if(want&&file_load(canonical,f->data,want)<0){free(f->data);f->used=0;errno=EIO;return -1;}if(flags&O_TRUNC){f->len=0;f->dirty=1;}}
    f->pos=0;f->mode=(flags&O_WRONLY)?1:((flags&O_RDWR)?2:0);f->eof=0;
    if(flags&O_TRUNC){f->len=0;f->dirty=1;}
    return (int)(f-files)+FD_BASE;
}
FILE *fdopen(int fd,const char*mode){KccFile*f=fdfile(fd);if(!f)return 0;if(mode&&(*mode=='w'||*mode=='a'))f->mode=1;else if(mode&&*mode=='+')f->mode=2;return(FILE*)f;}
int kcc_close(int fd){KccFile*f=fdfile(fd);if(!f){errno=EINVAL;return -1;}if(f->dirty&&file_save(f->name,f->data,f->len)<0){errno=EIO;return -1;}free(f->data);f->data=0;f->used=0;return 0;}
ssize_t kcc_write(int fd,const void*buf,size_t n){KccFile*f;size_t i,need,cap;unsigned char*p;if(fd==1||fd==2)return write(1,buf,n);f=fdfile(fd);if(!f||f->mode==0){errno=EINVAL;return -1;}need=(size_t)f->pos+n;if(need>FILE_CAP){errno=ENOMEM;return -1;}if(need>f->cap){cap=f->cap;while(cap<need){cap*=2;if(cap>FILE_CAP)cap=FILE_CAP;}p=realloc(f->data,cap);if(!p){errno=ENOMEM;return -1;}f->data=p;f->cap=cap;}if((size_t)f->pos>f->len)memset(f->data+f->len,0,(size_t)f->pos-f->len);for(i=0;i<n;i++)f->data[f->pos+i]=((const unsigned char*)buf)[i];f->pos+=n;if((size_t)f->pos>f->len)f->len=f->pos;f->dirty=1;return (ssize_t)n;}
ssize_t kcc_read(int fd,void *buf,size_t n){KccFile*f=fdfile(fd);size_t i;if(fd==0)return read(fd,buf,n);if(!f||f->mode==1){errno=EINVAL;return -1;}if(f->pos>=(long)f->len){f->eof=1;return 0;}if(n>f->len-(size_t)f->pos)n=f->len-(size_t)f->pos;for(i=0;i<n;i++)((unsigned char*)buf)[i]=f->data[f->pos+i];f->pos+=n;return (ssize_t)n;}
long kcc_lseek(int fd,long off,int whence){KccFile*f=fdfile(fd);long p;if(!f){errno=EINVAL;return -1;}p=whence==SEEK_SET?off:whence==SEEK_CUR?f->pos+off:whence==SEEK_END?(long)f->len+off:-1;if(p<0||(size_t)p>FILE_CAP){errno=EINVAL;return -1;}f->pos=p;f->eof=0;return p;}
int kcc_unlink(const char*n){(void)n;errno=EINVAL;return -1;} int kcc_access(const char*n,int m){char c[32];(void)m;if(!short_name(n,c,sizeof c))return -1;return file_load(c,0,0)<0?-1:0;}

FILE *fopen(const char *name,const char *mode){int flags=O_RDONLY,fd; if(mode&&(*mode=='w'||*mode=='a'))flags=O_WRONLY|O_CREAT|(*mode=='w'?O_TRUNC:0);else if(mode&&*mode=='+')flags=O_RDWR;fd=open(name,flags);return fd<0?0:(FILE*)&files[fd-FD_BASE];}
int fclose(FILE *fp){KccFile*f=(KccFile*)fp;if(f==&out_file||f==&err_file||f==&in_file)return 0;return close((int)(f-files)+FD_BASE);}
size_t fread(void *p,size_t s,size_t n,FILE *fp){ssize_t r;if(!s)return 0;r=read((int)((KccFile*)fp-files)+FD_BASE,p,s*n);return r<0?0:(size_t)r/s;}
size_t fwrite(const void*p,size_t s,size_t n,FILE*fp){KccFile*f=(KccFile*)fp;size_t total=s*n;ssize_t r;if(!s)return 0;if(f==&out_file||f==&err_file){write(1,p,total);return n;}r=kcc_write((int)(f-files)+FD_BASE,p,total);return r<0?0:(size_t)r/s;}
int fseek(FILE*fp,long o,int w){return lseek((int)((KccFile*)fp-files)+FD_BASE,o,w)<0?-1:0;} long ftell(FILE*fp){return lseek((int)((KccFile*)fp-files)+FD_BASE,0,SEEK_CUR);}
int fgetc(FILE*fp){unsigned char c;return fread(&c,1,1,fp)==1?c:-1;} int getc(FILE*fp){return fgetc(fp);} int ungetc(int c,FILE*fp){KccFile*f=(KccFile*)fp;if(!f||f->pos<=0)return -1;f->data[--f->pos]=(unsigned char)c;f->eof=0;return c;} int fputc(int c,FILE*fp){unsigned char x=(unsigned char)c;return fwrite(&x,1,1,fp)==1?x:-1;}
int feof(FILE*fp){return ((KccFile*)fp)->eof;} int fflush(FILE*fp){(void)fp;return 0;}
int kcc_vfprintf(FILE*fp,const char*fmt,va_list ap){char b[1024];int n=format(b,sizeof b,fmt,ap);if(n>0){size_t z=(size_t)n;if(z>=sizeof b)z=sizeof b-1;fwrite(b,1,z,fp);}return n;}
int kcc_fprintf(FILE*fp,const char*fmt,...){va_list ap;int n;va_start(ap,fmt);n=kcc_vfprintf(fp,fmt,ap);va_end(ap);return n;}
int kcc_printf(const char*fmt,...){va_list ap;int n;va_start(ap,fmt);n=kcc_vfprintf(stdout,fmt,ap);va_end(ap);return n;}
int kcc_vsnprintf(char*b,size_t z,const char*f,va_list ap){return format(b,z,f,ap);}
int kcc_snprintf(char*b,size_t z,const char*f,...){va_list ap;int n;va_start(ap,f);n=format(b,z,f,ap);va_end(ap);return n;}
int kcc_puts(const char*s){size_t n=strlen(s);fwrite(s,1,n,stdout);fwrite("\n",1,1,stdout);return 0;} int kcc_fputs(const char*s,FILE*f){fwrite(s,1,strlen(s),f);return 0;}
void kcc_perror(const char*s){kcc_fprintf(stderr,"%s\n",s?s:"error");}
int gettimeofday(struct timeval*t,void*z){(void)z;if(t){t->tv_sec=0;t->tv_usec=0;}return 0;} time_t time(time_t*t){if(t)*t=0;return 0;} clock_t clock(void){return 0;}
char *getenv(const char*n){(void)n;return 0;} char *kcc_getcwd(char*b,size_t n){if(n)b[0]=0;return b;} int abs(int x){return x<0?-x:x;} long labs(long x){return x<0?-x:x;}
struct tm *localtime(const time_t*t){static struct tm d; (void)t;d.tm_year=126;d.tm_mon=0;d.tm_mday=1;return &d;}

static void emit(char*out,size_t cap,size_t*pos,char c){if(cap&&*pos<cap-1)out[*pos]=c;(*pos)++;}
static void number(char*out,size_t cap,size_t*pos,unsigned long long v,unsigned base,int upper,int width,int left,char pad){char t[32];int n=0,i;do{unsigned d=(unsigned)(v%base);t[n++]=(char)(d<10?'0'+d:(upper?'A':'a')+d-10);v/=base;}while(v&&n<32);while(!left&&width>n){emit(out,cap,pos,pad);width--;}for(i=n-1;i>=0;i--)emit(out,cap,pos,t[i]);while(left&&width>n){emit(out,cap,pos,' ');width--;}}
static int format(char*out,size_t cap,const char*fmt,va_list ap){size_t pos=0;while(*fmt){if(*fmt!='%'){emit(out,cap,&pos,*fmt++);continue;}fmt++;if(*fmt=='%'){emit(out,cap,&pos,*fmt++);continue;}int left=0,width=0,ln=0;char pad=' ';if(*fmt=='-'){left=1;fmt++;}if(*fmt=='0'){pad='0';fmt++;}while(*fmt>='0'&&*fmt<='9'){width=width*10+*fmt++-'0';if(width>128)width=128;}if(*fmt=='l'){ln=1;fmt++;if(*fmt=='l'){ln=2;fmt++;}}char c=*fmt?*fmt++:0;switch(c){case 's':{const char*s=va_arg(ap,const char*);if(!s)s="(null)";size_t n=strlen(s);while(!left&&width>(int)n){emit(out,cap,&pos,' ');width--;}while(*s)emit(out,cap,&pos,*s++);while(left&&width>(int)n){emit(out,cap,&pos,' ');width--;}break;}case 'c':emit(out,cap,&pos,(char)va_arg(ap,int));break;case 'd':case 'i':{long long v=ln==2?va_arg(ap,long long):ln?va_arg(ap,long):va_arg(ap,int);unsigned long long u;if(v<0){emit(out,cap,&pos,'-');width--;u=(unsigned long long)(-v);}else u=(unsigned long long)v;number(out,cap,&pos,u,10,0,width,left,pad);break;}case 'u':case 'x':case 'X':case 'o':{unsigned long long v=ln==2?va_arg(ap,unsigned long long):ln?va_arg(ap,unsigned long):va_arg(ap,unsigned int);number(out,cap,&pos,v,c=='o'?8:c=='u'?10:16,c=='X',width,left,pad);break;}case 'p':number(out,cap,&pos,(uintptr_t)va_arg(ap,void*),16,0,width,left,pad);break;default:emit(out,cap,&pos,'%');if(c)emit(out,cap,&pos,c);break;}}
if(cap)out[pos<cap-1?pos:cap-1]=0;return (int)pos;}
int kcc_sprintf(char*out,const char*fmt,...){va_list ap;int n;va_start(ap,fmt);n=format(out,(size_t)-1,fmt,ap);va_end(ap);return n;}
int kcc_sscanf(const char*in,const char*fmt,...){va_list ap;int n=0;va_start(ap,fmt);while(*fmt){if(*fmt!='%'){if(*in==*fmt){in++;fmt++;continue;}break;}fmt++;if(*fmt=='d'){int*p=va_arg(ap,int*);int v=0;if(*in<'0'||*in>'9')break;while(*in>='0'&&*in<='9')v=v*10+*in++-'0';*p=v;n++;fmt++;continue;}if(*fmt=='s'){char*p=va_arg(ap,char*);while(*in&&*in!=' ') *p++=*in++;*p=0;n++;fmt++;continue;}break;}va_end(ap);return n;}
int atoi(const char*s){return (int)strtol(s,0,10);}
double ldexp(double x,int e){while(e>0){x*=2.0;e--;}while(e<0){x*=0.5;e++;}return x;}
int mprotect(void*p,unsigned long n,int prot){(void)p;(void)n;(void)prot;return 0;}
