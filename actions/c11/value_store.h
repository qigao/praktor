#ifndef PRAKTOR_C11_VALUE_STORE_H
#define PRAKTOR_C11_VALUE_STORE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct praktor_c11_value_store { void *impl; } praktor_c11_value_store;
enum { PRAKTOR_C11_VALUE_OK=0, PRAKTOR_C11_VALUE_EINVAL=1, PRAKTOR_C11_VALUE_ENOMEM=2, PRAKTOR_C11_VALUE_ENOSPC=3, PRAKTOR_C11_VALUE_ENOBUFS=4, PRAKTOR_C11_VALUE_ENOTFOUND=5 };
int praktor_c11_value_store_init(praktor_c11_value_store *,size_t,size_t,size_t);
void praktor_c11_value_store_destroy(praktor_c11_value_store *);
int praktor_c11_value_store_set(praktor_c11_value_store *,const char *,const char *);
int praktor_c11_value_store_get(const praktor_c11_value_store *,const char *,char *,size_t,size_t *);
int praktor_c11_value_store_contains(const praktor_c11_value_store *,const char *,int *);
int praktor_c11_value_store_erase(praktor_c11_value_store *,const char *);
size_t praktor_c11_value_store_size(const praktor_c11_value_store *);
#ifdef __cplusplus
}
#endif
#endif
