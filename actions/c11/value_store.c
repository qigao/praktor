#include "value_store.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef struct entry { char *key,*value; size_t value_size; int occupied; } entry;
typedef struct impl { size_t cap,mk,mv,size; entry *e; char *keys,*values; } impl;
static impl *get(const praktor_c11_value_store*s){return s?(impl*)s->impl:NULL;}
static size_t len(const char*s,size_t max){size_t n=0;if(!s)return SIZE_MAX;while(n<=max&&s[n]!='\0')++n;return n;}
static entry *find(impl*p,const char*k,size_t n){size_t i;for(i=0;i<p->cap;++i)if(p->e[i].occupied&&strlen(p->e[i].key)==n&&!memcmp(p->e[i].key,k,n))return &p->e[i];return NULL;}
int praktor_c11_value_store_init(praktor_c11_value_store*s,size_t cap,size_t mk,size_t mv){impl*p;size_t i;if(!s||s->impl||!cap||!mk||!mv||mk==SIZE_MAX||mv==SIZE_MAX||cap>SIZE_MAX/sizeof(entry)||cap>SIZE_MAX/(mk+1)||cap>SIZE_MAX/(mv+1))return PRAKTOR_C11_VALUE_EINVAL;p=calloc(1,sizeof(*p));if(!p)return PRAKTOR_C11_VALUE_ENOMEM;p->e=calloc(cap,sizeof(entry));p->keys=calloc(cap,mk+1);p->values=calloc(cap,mv+1);if(!p->e||!p->keys||!p->values){free(p->e);free(p->keys);free(p->values);free(p);return PRAKTOR_C11_VALUE_ENOMEM;}p->cap=cap;p->mk=mk;p->mv=mv;for(i=0;i<cap;++i){p->e[i].key=p->keys+i*(mk+1);p->e[i].value=p->values+i*(mv+1);}s->impl=p;return 0;}
void praktor_c11_value_store_destroy(praktor_c11_value_store*s){impl*p=get(s);if(!p)return;free(p->e);free(p->keys);free(p->values);free(p);s->impl=NULL;}
int praktor_c11_value_store_set(praktor_c11_value_store*s,const char*k,const char*v){impl*p=get(s);size_t kn,vn,i;entry*e;if(!p)return 1;kn=len(k,p->mk);vn=len(v,p->mv);if(!kn||kn==SIZE_MAX)return 1;if(kn>p->mk||vn>p->mv)return 4;e=find(p,k,kn);if(!e){for(i=0;i<p->cap&&!e;++i)if(!p->e[i].occupied)e=&p->e[i];if(!e)return 3;memcpy(e->key,k,kn);e->key[kn]=0;e->occupied=1;++p->size;}memcpy(e->value,v,vn);e->value[vn]=0;e->value_size=vn;return 0;}
int praktor_c11_value_store_get(const praktor_c11_value_store*s,const char*k,char*out,size_t cap,size_t*n){impl*p=get(s);size_t kn;entry*e;if(!p||!n)return 1;kn=len(k,p->mk);if(!kn||kn>p->mk||kn==SIZE_MAX)return 1;e=find(p,k,kn);if(!e)return 5;*n=e->value_size;if(!out||cap<=e->value_size)return 4;memcpy(out,e->value,e->value_size+1);return 0;}
int praktor_c11_value_store_contains(const praktor_c11_value_store*s,const char*k,int*y){impl*p=get(s);size_t n;if(!p||!y)return 1;n=len(k,p->mk);if(!n||n>p->mk||n==SIZE_MAX)return 1;*y=find(p,k,n)!=NULL;return 0;}
int praktor_c11_value_store_erase(praktor_c11_value_store*s,const char*k){impl*p=get(s);size_t n;entry*e;if(!p)return 1;n=len(k,p->mk);if(!n||n>p->mk||n==SIZE_MAX)return 1;e=find(p,k,n);if(!e)return 5;memset(e->key,0,p->mk+1);memset(e->value,0,p->mv+1);e->value_size=0;e->occupied=0;--p->size;return 0;}
size_t praktor_c11_value_store_size(const praktor_c11_value_store*s){impl*p=get(s);return p?p->size:0;}
