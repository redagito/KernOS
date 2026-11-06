#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

void *memmove(void*d,const void*s,size_t n){unsigned char*o=d;const unsigned char*i=s;if(o<i){while(n--)*o++=*i++;}else if(o>i){o+=n;i+=n;while(n--)*--o=*--i;}return d;}
int memcmp(const void*a,const void*b,size_t n){const unsigned char*x=a,*y=b;while(n--){if(*x!=*y)return *x-*y;x++;y++;}return 0;}
char *strncpy(char*d,const char*s,size_t n){size_t i=0;for(;i<n&&s[i];i++)d[i]=s[i];for(;i<n;i++)d[i]=0;return d;}
char *strcat(char*d,const char*s){char*r=d+strlen(d);while((*r++=*s++));return d;}
char *strchr(const char*s,int c){for(;;s++){if(*s==(char)c)return (char*)s;if(!*s)return 0;}}
char *strrchr(const char*s,int c){const char*r=0;for(;;s++){if(*s==(char)c)r=s;if(!*s)return (char*)r;}}
char *strstr(const char*h,const char*n){size_t z=strlen(n);if(!z)return(char*)h;for(;*h;h++)if(!strncmp(h,n,z))return(char*)h;return 0;}
char *strdup(const char*s){size_t n=strlen(s)+1;char*p=malloc(n);if(p)memcpy(p,s,n);return p;}
static char *tok;
char *strtok(char*s,const char*del){if(s)tok=s;else if(!tok)return 0;while(*tok&&strchr(del,*tok))tok++;if(!*tok){tok=0;return 0;}char*r=tok;while(*tok&&!strchr(del,*tok))tok++;if(*tok)*tok++=0;return r;}
unsigned long strtoul(const char*s,char**end,int base){const char*start=s;unsigned long v=0;int neg=0,d,any=0;if(*s=='-'||*s=='+'){neg=*s=='-';s++;}if(!base){if(s[0]=='0'&&(s[1]=='x'||s[1]=='X')){base=16;s+=2;}else if(s[0]=='0')base=8;else base=10;}else if(base==16&&s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s+=2;if(base<2||base>36){if(end)*end=(char*)start;return 0;}for(;;s++){d=*s>='0'&&*s<='9'?*s-'0':*s>='a'&&*s<='z'?*s-'a'+10:*s>='A'&&*s<='Z'?*s-'A'+10:99;if(d>=base)break;v=v*(unsigned)base+(unsigned)d;any=1;}if(end)*end=(char*)(any?s:start);return neg?(unsigned long)(0-v):v;}
long strtol(const char*s,char**e,int b){return(long)strtoul(s,e,b);} unsigned long long strtoull(const char*s,char**e,int b){return(unsigned long long)strtoul(s,e,b);} long long strtoll(const char*s,char**e,int b){return(long long)strtoul(s,e,b);}
static long double parse_ld(const char*s,char**end){const char*start=s;long double v=0.0L,scale=1.0L;int neg=0,digits=0,exp=0,exneg=0;if(*s=='-'||*s=='+'){neg=*s=='-';s++;}while(*s>='0'&&*s<='9'){v=v*10.0L+(*s++-'0');digits++;}if(*s=='.'){s++;while(*s>='0'&&*s<='9'){v=v*10.0L+(*s++-'0');scale*=10.0L;digits++;}}if(!digits){if(end)*end=(char*)start;return 0.0L;}if(*s=='e'||*s=='E'){const char*q=s++;if(*s=='-'||*s=='+'){exneg=*s=='-';s++;}if(*s<'0'||*s>'9')s=q;else{while(*s>='0'&&*s<='9'){if(exp<10000)exp=exp*10+(*s-'0');s++;}if(exneg)exp=-exp;}}v/=scale;if(exp>400)exp=400;if(exp< -400)exp=-400;while(exp>0){v*=10.0L;exp--;}while(exp<0){v/=10.0L;exp++;}if(end)*end=(char*)s;return neg?-v:v;}
double strtod(const char*s,char**e){return(double)parse_ld(s,e);} float strtof(const char*s,char**e){return(float)parse_ld(s,e);} long double strtold(const char*s,char**e){return parse_ld(s,e);}
void qsort(void*base,size_t n,size_t z,int(*cmp)(const void*,const void*)){size_t i,j;unsigned char*t=malloc(z);if(!t)return;for(i=1;i<n;i++)for(j=i;j>0&&cmp((char*)base+(j-1)*z,(char*)base+j*z)>0;j--){memcpy(t,(char*)base+(j-1)*z,z);memcpy((char*)base+(j-1)*z,(char*)base+j*z,z);memcpy((char*)base+j*z,t,z);}free(t);}
void __kcc_assert(const char*x,const char*f,int l){fprintf(stderr,"assertion failed: %s (%s:%d)\n",x,f,l);exit(1);}
