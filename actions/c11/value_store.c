#include "value_store.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct praktor_c11_value_entry {
  char *key;
  char *value;
  size_t value_size;
  int occupied;
} praktor_c11_value_entry;

typedef struct praktor_c11_value_store_impl {
  size_t capacity;
  size_t max_key_bytes;
  size_t max_value_bytes;
  size_t size;
  praktor_c11_value_entry *entries;
  char *key_storage;
  char *value_storage;
} praktor_c11_value_store_impl;

static praktor_c11_value_store_impl *store_impl(
    const praktor_c11_value_store *store) {
  return store == NULL ? NULL
                       : (praktor_c11_value_store_impl *)store->impl;
}

/* Return SIZE_MAX for NULL; otherwise stop after max_bytes + 1 bytes. */
static size_t bounded_length(const char *text, size_t max_bytes) {
  size_t length = 0u;
  if (text == NULL) return SIZE_MAX;
  while (length <= max_bytes && text[length] != '\0') ++length;
  return length;
}

static praktor_c11_value_entry *find_entry(
    praktor_c11_value_store_impl *impl, const char *key, size_t key_size) {
  size_t i;
  for (i = 0u; i < impl->capacity; ++i) {
    praktor_c11_value_entry *entry = &impl->entries[i];
    if (entry->occupied && strlen(entry->key) == key_size &&
        memcmp(entry->key, key, key_size) == 0) {
      return entry;
    }
  }
  return NULL;
}

int praktor_c11_value_store_init(praktor_c11_value_store *store,
                                 size_t capacity,
                                 size_t max_key_bytes,
                                 size_t max_value_bytes) {
  praktor_c11_value_store_impl *impl;
  size_t i;

  if (store == NULL || store->impl != NULL || capacity == 0u ||
      max_key_bytes == 0u || max_value_bytes == 0u ||
      max_key_bytes == SIZE_MAX || max_value_bytes == SIZE_MAX ||
      capacity > SIZE_MAX / sizeof(praktor_c11_value_entry) ||
      capacity > SIZE_MAX / (max_key_bytes + 1u) ||
      capacity > SIZE_MAX / (max_value_bytes + 1u)) {
    return PRAKTOR_C11_VALUE_EINVAL;
  }

  impl = (praktor_c11_value_store_impl *)calloc(1u, sizeof(*impl));
  if (impl == NULL) return PRAKTOR_C11_VALUE_ENOMEM;

  impl->entries = (praktor_c11_value_entry *)calloc(
      capacity, sizeof(*impl->entries));
  impl->key_storage = (char *)calloc(capacity, max_key_bytes + 1u);
  impl->value_storage = (char *)calloc(capacity, max_value_bytes + 1u);
  if (impl->entries == NULL || impl->key_storage == NULL ||
      impl->value_storage == NULL) {
    free(impl->entries);
    free(impl->key_storage);
    free(impl->value_storage);
    free(impl);
    return PRAKTOR_C11_VALUE_ENOMEM;
  }

  impl->capacity = capacity;
  impl->max_key_bytes = max_key_bytes;
  impl->max_value_bytes = max_value_bytes;
  for (i = 0u; i < capacity; ++i) {
    impl->entries[i].key = impl->key_storage + i * (max_key_bytes + 1u);
    impl->entries[i].value = impl->value_storage + i * (max_value_bytes + 1u);
  }
  store->impl = impl;
  return PRAKTOR_C11_VALUE_OK;
}

int praktor_c11_value_store_clone(praktor_c11_value_store *destination,
                                  const praktor_c11_value_store *source) {
  const praktor_c11_value_store_impl *src = store_impl(source);
  praktor_c11_value_store_impl *dst;
  size_t i;
  if (destination == NULL || source == NULL || src == NULL ||
      destination == source || destination->impl != NULL) {
    return PRAKTOR_C11_VALUE_EINVAL;
  }
  if (praktor_c11_value_store_init(destination, src->capacity,
                                   src->max_key_bytes,
                                   src->max_value_bytes) != PRAKTOR_C11_VALUE_OK) {
    return PRAKTOR_C11_VALUE_ENOMEM;
  }
  dst = store_impl(destination);
  for (i = 0u; i < src->capacity; ++i) {
    const praktor_c11_value_entry *from = &src->entries[i];
    praktor_c11_value_entry *to = &dst->entries[i];
    if (!from->occupied) continue;
    memcpy(to->key, from->key, strlen(from->key) + 1u);
    memcpy(to->value, from->value, from->value_size + 1u);
    to->value_size = from->value_size;
    to->occupied = 1;
    ++dst->size;
  }
  return PRAKTOR_C11_VALUE_OK;
}

/* Bounded native value-store transfer. A live Session must never observe
 * this handle replacement while its leaf callbacks are in progress. */
int praktor_c11_value_store_replace_clone(praktor_c11_value_store *destination,
                                          const praktor_c11_value_store *source) {
  const praktor_c11_value_store_impl *dst = store_impl(destination);
  const praktor_c11_value_store_impl *src = store_impl(source);
  praktor_c11_value_store staging = {0};
  void *previous;
  int status;
  if (!dst || !src || destination == source ||
      dst->capacity != src->capacity ||
      dst->max_key_bytes != src->max_key_bytes ||
      dst->max_value_bytes != src->max_value_bytes)
    return PRAKTOR_C11_VALUE_EINVAL;
  status = praktor_c11_value_store_clone(&staging, source);
  if (status != PRAKTOR_C11_VALUE_OK) return status;
  previous = destination->impl;
  destination->impl = staging.impl;
  staging.impl = previous;
  praktor_c11_value_store_destroy(&staging);
  return PRAKTOR_C11_VALUE_OK;
}

void praktor_c11_value_store_destroy(praktor_c11_value_store *store) {
  praktor_c11_value_store_impl *impl = store_impl(store);
  if (impl == NULL) return;
  free(impl->entries);
  free(impl->key_storage);
  free(impl->value_storage);
  free(impl);
  store->impl = NULL;
}

int praktor_c11_value_store_set(praktor_c11_value_store *store,
                                const char *key, const char *value) {
  praktor_c11_value_store_impl *impl = store_impl(store);
  size_t key_size, value_size, i;
  praktor_c11_value_entry *entry;

  if (impl == NULL || key == NULL || value == NULL)
    return PRAKTOR_C11_VALUE_EINVAL;

  key_size = bounded_length(key, impl->max_key_bytes);
  value_size = bounded_length(value, impl->max_value_bytes);
  if (key_size == 0u || key_size == SIZE_MAX)
    return PRAKTOR_C11_VALUE_EINVAL;
  if (key_size > impl->max_key_bytes ||
      value_size > impl->max_value_bytes) {
    return PRAKTOR_C11_VALUE_ENOBUFS;
  }

  entry = find_entry(impl, key, key_size);
  if (entry == NULL) {
    for (i = 0u; i < impl->capacity; ++i) {
      if (!impl->entries[i].occupied) {
        entry = &impl->entries[i];
        break;
      }
    }
    if (entry == NULL) return PRAKTOR_C11_VALUE_ENOSPC;
    memcpy(entry->key, key, key_size);
    entry->key[key_size] = '\0';
    entry->occupied = 1;
    ++impl->size;
  }

  /* All input/capacity validation precedes mutation: failed update is atomic. */
  memcpy(entry->value, value, value_size);
  entry->value[value_size] = '\0';
  entry->value_size = value_size;
  return PRAKTOR_C11_VALUE_OK;
}

int praktor_c11_value_store_set_many(praktor_c11_value_store *store,
                                     const praktor_c11_value_write *writes,
                                     size_t count) {
  praktor_c11_value_store_impl *impl = store_impl(store);
  size_t i, j, needed = 0u;
  if (impl == NULL || (count != 0u && writes == NULL))
    return PRAKTOR_C11_VALUE_EINVAL;
  /* Distinct keys cannot fit in fewer slots than the write count. This also
   * bounds the quadratic duplicate check by the store's configured capacity. */
  if (count > impl->capacity) return PRAKTOR_C11_VALUE_ENOSPC;
  for (i = 0u; i < count; ++i) {
    size_t key_size, value_size;
    if (writes[i].key == NULL || writes[i].value == NULL)
      return PRAKTOR_C11_VALUE_EINVAL;
    key_size = bounded_length(writes[i].key, impl->max_key_bytes);
    value_size = bounded_length(writes[i].value, impl->max_value_bytes);
    if (key_size == 0u) return PRAKTOR_C11_VALUE_EINVAL;
    if (key_size > impl->max_key_bytes || value_size > impl->max_value_bytes)
      return PRAKTOR_C11_VALUE_ENOBUFS;
    for (j = 0u; j < i; ++j)
      if (strcmp(writes[i].key, writes[j].key) == 0)
        return PRAKTOR_C11_VALUE_EINVAL;
    if (find_entry(impl, writes[i].key, key_size) == NULL) ++needed;
  }
  if (needed > impl->capacity - impl->size) return PRAKTOR_C11_VALUE_ENOSPC;
  /* All inputs and capacity are now valid. Single-driver ownership prevents
   * any interleaved observation or mutation during publication. */
  for (i = 0u; i < count; ++i)
    (void)praktor_c11_value_store_set(store, writes[i].key, writes[i].value);
  return PRAKTOR_C11_VALUE_OK;
}

int praktor_c11_value_store_get(const praktor_c11_value_store *store,
                                const char *key, char *out_value,
                                size_t out_capacity, size_t *out_size) {
  praktor_c11_value_store_impl *impl = store_impl(store);
  size_t key_size;
  praktor_c11_value_entry *entry;

  if (impl == NULL || key == NULL || out_size == NULL)
    return PRAKTOR_C11_VALUE_EINVAL;
  key_size = bounded_length(key, impl->max_key_bytes);
  if (key_size == 0u || key_size > impl->max_key_bytes ||
      key_size == SIZE_MAX) {
    return PRAKTOR_C11_VALUE_EINVAL;
  }

  entry = find_entry(impl, key, key_size);
  if (entry == NULL) return PRAKTOR_C11_VALUE_ENOTFOUND;
  *out_size = entry->value_size;
  if (out_value == NULL || out_capacity <= entry->value_size)
    return PRAKTOR_C11_VALUE_ENOBUFS;
  memcpy(out_value, entry->value, entry->value_size + 1u);
  return PRAKTOR_C11_VALUE_OK;
}

int praktor_c11_value_store_contains(const praktor_c11_value_store *store,
                                     const char *key, int *out_contains) {
  praktor_c11_value_store_impl *impl = store_impl(store);
  size_t key_size;

  if (impl == NULL || key == NULL || out_contains == NULL)
    return PRAKTOR_C11_VALUE_EINVAL;
  key_size = bounded_length(key, impl->max_key_bytes);
  if (key_size == 0u || key_size > impl->max_key_bytes ||
      key_size == SIZE_MAX) {
    return PRAKTOR_C11_VALUE_EINVAL;
  }
  *out_contains = find_entry(impl, key, key_size) != NULL;
  return PRAKTOR_C11_VALUE_OK;
}

int praktor_c11_value_store_erase(praktor_c11_value_store *store,
                                  const char *key) {
  praktor_c11_value_store_impl *impl = store_impl(store);
  size_t key_size;
  praktor_c11_value_entry *entry;

  if (impl == NULL || key == NULL) return PRAKTOR_C11_VALUE_EINVAL;
  key_size = bounded_length(key, impl->max_key_bytes);
  if (key_size == 0u || key_size > impl->max_key_bytes ||
      key_size == SIZE_MAX) {
    return PRAKTOR_C11_VALUE_EINVAL;
  }
  entry = find_entry(impl, key, key_size);
  if (entry == NULL) return PRAKTOR_C11_VALUE_ENOTFOUND;

  memset(entry->key, 0, impl->max_key_bytes + 1u);
  memset(entry->value, 0, impl->max_value_bytes + 1u);
  entry->value_size = 0u;
  entry->occupied = 0;
  --impl->size;
  return PRAKTOR_C11_VALUE_OK;
}

typedef struct praktor_c11_value_txn_op {
  char *key;
  size_t key_size;
  char *baseline_value;
  size_t baseline_value_size;
  int baseline_exists;
} praktor_c11_value_txn_op;
typedef struct praktor_c11_value_txn_impl {
  praktor_c11_value_store *target;
  praktor_c11_value_store baseline, working;
  size_t op_count, op_capacity;
  praktor_c11_value_txn_op *ops;
  char *op_keys, *op_values;
} praktor_c11_value_txn_impl;
static praktor_c11_value_entry *find_entry_const(const praktor_c11_value_store_impl *p,const char*k,size_t n){size_t i;for(i=0;i<p->capacity;++i){praktor_c11_value_entry*e=(praktor_c11_value_entry*)&p->entries[i];if(e->occupied&&strlen(e->key)==n&&!memcmp(e->key,k,n))return e;}return NULL;}
static int same_key(const praktor_c11_value_store*a,const praktor_c11_value_store*b,const char*k,size_t n){const praktor_c11_value_store_impl*ai=store_impl(a),*bi=store_impl(b);praktor_c11_value_entry*ae=find_entry_const(ai,k,n),*be=find_entry_const(bi,k,n);if(!ae||!be)return ae==be;return ae->value_size==be->value_size&&!memcmp(ae->value,be->value,ae->value_size);}
static praktor_c11_value_txn_impl *get_txn(praktor_c11_value_txn*t){return t?(praktor_c11_value_txn_impl*)t->impl:NULL;}
static praktor_c11_value_txn_op *txn_find_op(praktor_c11_value_txn_impl*t,const char*k,size_t n){size_t i;for(i=0;i<t->op_count;++i)if(t->ops[i].key_size==n&&!memcmp(t->ops[i].key,k,n))return &t->ops[i];return NULL;}
static int txn_record(praktor_c11_value_txn_impl*t,const char*k,size_t n){praktor_c11_value_txn_op*o=txn_find_op(t,k,n);praktor_c11_value_store_impl*b;praktor_c11_value_entry*e;size_t slot;if(o)return PRAKTOR_C11_VALUE_OK;if(t->op_count>=t->op_capacity)return PRAKTOR_C11_VALUE_ENOSPC;slot=t->op_count++;o=&t->ops[slot];b=store_impl(&t->baseline);o->key=t->op_keys+slot*(b->max_key_bytes+1u);o->baseline_value=t->op_values+slot*(b->max_value_bytes+1u);memcpy(o->key,k,n);o->key[n]=0;o->key_size=n;e=find_entry(b,k,n);o->baseline_exists=e!=NULL;if(e){o->baseline_value_size=e->value_size;memcpy(o->baseline_value,e->value,e->value_size+1u);}return PRAKTOR_C11_VALUE_OK;}
int praktor_c11_value_txn_begin(praktor_c11_value_txn*t,praktor_c11_value_store*target){praktor_c11_value_txn_impl*p;praktor_c11_value_store_impl*base=store_impl(target);int r;if(!t||t->impl||!base)return PRAKTOR_C11_VALUE_EINVAL;p=(praktor_c11_value_txn_impl*)calloc(1,sizeof(*p));if(!p)return PRAKTOR_C11_VALUE_ENOMEM;p->target=target;p->op_capacity=base->capacity;p->ops=(praktor_c11_value_txn_op*)calloc(p->op_capacity,sizeof(*p->ops));p->op_keys=(char*)calloc(p->op_capacity,base->max_key_bytes+1u);p->op_values=(char*)calloc(p->op_capacity,base->max_value_bytes+1u);if(!p->ops||!p->op_keys||!p->op_values){free(p->ops);free(p->op_keys);free(p->op_values);free(p);return PRAKTOR_C11_VALUE_ENOMEM;}r=praktor_c11_value_store_clone(&p->baseline,target);if(r==0)r=praktor_c11_value_store_clone(&p->working,target);if(r!=0){praktor_c11_value_store_destroy(&p->baseline);praktor_c11_value_store_destroy(&p->working);free(p->ops);free(p->op_keys);free(p->op_values);free(p);return r;}t->impl=p;return 0;}
int praktor_c11_value_txn_set(praktor_c11_value_txn*t,const char*k,const char*v){praktor_c11_value_txn_impl*p=get_txn(t);praktor_c11_value_store_impl*w;size_t kn,vn;int r;if(!p||!k||!v)return PRAKTOR_C11_VALUE_EINVAL;w=store_impl(&p->working);kn=bounded_length(k,w->max_key_bytes);vn=bounded_length(v,w->max_value_bytes);if(!kn||kn==SIZE_MAX)return PRAKTOR_C11_VALUE_EINVAL;if(kn>w->max_key_bytes||vn>w->max_value_bytes)return PRAKTOR_C11_VALUE_ENOBUFS;if(!find_entry(w,k,kn)&&w->size>=w->capacity)return PRAKTOR_C11_VALUE_ENOSPC;r=txn_record(p,k,kn);if(r)return r;return praktor_c11_value_store_set(&p->working,k,v);}
int praktor_c11_value_txn_erase(praktor_c11_value_txn*t,const char*k){praktor_c11_value_txn_impl*p=get_txn(t);praktor_c11_value_store_impl*w;size_t kn;int r;if(!p||!k)return PRAKTOR_C11_VALUE_EINVAL;w=store_impl(&p->working);kn=bounded_length(k,w->max_key_bytes);if(!kn||kn==SIZE_MAX||kn>w->max_key_bytes)return PRAKTOR_C11_VALUE_EINVAL;if(!find_entry(w,k,kn))return PRAKTOR_C11_VALUE_ENOTFOUND;r=txn_record(p,k,kn);if(r)return r;return praktor_c11_value_store_erase(&p->working,k);}
int praktor_c11_value_txn_commit(praktor_c11_value_txn*t){praktor_c11_value_txn_impl*p=get_txn(t);praktor_c11_value_store staged={0};praktor_c11_value_store_impl*w;size_t i;int r;if(!p||!store_impl(p->target))return PRAKTOR_C11_VALUE_EINVAL;for(i=0;i<p->op_count;++i)if(!same_key(p->target,&p->baseline,p->ops[i].key,p->ops[i].key_size))return PRAKTOR_C11_VALUE_ECONFLICT;r=praktor_c11_value_store_clone(&staged,p->target);if(r)return r;w=store_impl(&p->working);for(i=0;i<p->op_count;++i){praktor_c11_value_txn_op*o=&p->ops[i];praktor_c11_value_entry*e=find_entry(w,o->key,o->key_size);if(e)r=praktor_c11_value_store_set(&staged,o->key,e->value);else{int yes=0;r=praktor_c11_value_store_contains(&staged,o->key,&yes);if(!r&&yes)r=praktor_c11_value_store_erase(&staged,o->key);}if(r)break;}if(r){praktor_c11_value_store_destroy(&staged);return r;}{void*old=p->target->impl;p->target->impl=staged.impl;staged.impl=old;}praktor_c11_value_store_destroy(&staged);praktor_c11_value_txn_abort(t);return 0;}
void praktor_c11_value_txn_abort(praktor_c11_value_txn*t){praktor_c11_value_txn_impl*p=get_txn(t);if(!p)return;praktor_c11_value_store_destroy(&p->baseline);praktor_c11_value_store_destroy(&p->working);free(p->ops);free(p->op_keys);free(p->op_values);free(p);t->impl=NULL;}
size_t praktor_c11_value_store_size(const praktor_c11_value_store *store) {
  const praktor_c11_value_store_impl *impl =
      store == NULL ? NULL : (const praktor_c11_value_store_impl *)store->impl;
  return impl == NULL ? 0u : impl->size;
}

int praktor_c11_value_txn_commit_group(praktor_c11_value_txn *txns, size_t count) {
  praktor_c11_value_txn_impl **items;
  praktor_c11_value_store staged = {0};
  praktor_c11_value_store *target = NULL;
  size_t i, j, k;
  int status;
  if (txns == NULL || count == 0u || count > SIZE_MAX / sizeof(*items))
    return PRAKTOR_C11_VALUE_EINVAL;
  items = (praktor_c11_value_txn_impl **)calloc(count, sizeof(*items));
  if (items == NULL) return PRAKTOR_C11_VALUE_ENOMEM;
  for (i = 0u; i < count; ++i) {
    items[i] = get_txn(&txns[i]);
    if (items[i] == NULL || store_impl(items[i]->target) == NULL ||
        (target != NULL && items[i]->target != target)) {
      free(items); return PRAKTOR_C11_VALUE_EINVAL;
    }
    target = items[i]->target;
    for (j = 0u; j < i; ++j) {
      if (items[j] == items[i]) {
        free(items);
        return PRAKTOR_C11_VALUE_EINVAL;
      }
    }
  }
  for (i = 0u; i < count; ++i) for (j = 0u; j < items[i]->op_count; ++j) {
    const praktor_c11_value_txn_op *a = &items[i]->ops[j];
    if (!same_key(target, &items[i]->baseline, a->key, a->key_size)) {
      free(items); return PRAKTOR_C11_VALUE_ECONFLICT;
    }
    for (k = i + 1u; k < count; ++k) {
      size_t m;
      for (m = 0u; m < items[k]->op_count; ++m) {
        const praktor_c11_value_txn_op *b = &items[k]->ops[m];
        if (a->key_size == b->key_size &&
            memcmp(a->key, b->key, a->key_size) == 0) {
          free(items); return PRAKTOR_C11_VALUE_ECONFLICT;
        }
      }
    }
  }
  status = praktor_c11_value_store_clone(&staged, target);
  if (status != PRAKTOR_C11_VALUE_OK) { free(items); return status; }
  /* Deletions first allow fixed-capacity slots to be reused. */
  for (i = 0u; i < count; ++i) for (j = 0u; j < items[i]->op_count; ++j) {
    const praktor_c11_value_txn_op *op = &items[i]->ops[j];
    if (find_entry(store_impl(&items[i]->working), op->key, op->key_size) == NULL) {
      int present = 0;
      status = praktor_c11_value_store_contains(&staged, op->key, &present);
      if (status == PRAKTOR_C11_VALUE_OK && present)
        status = praktor_c11_value_store_erase(&staged, op->key);
      if (status != PRAKTOR_C11_VALUE_OK) {
        praktor_c11_value_store_destroy(&staged); free(items); return status;
      }
    }
  }
  /* Array order is deterministic for disjoint writes; no last-writer policy. */
  for (i = 0u; i < count; ++i) for (j = 0u; j < items[i]->op_count; ++j) {
    const praktor_c11_value_txn_op *op = &items[i]->ops[j];
    praktor_c11_value_entry *entry =
        find_entry(store_impl(&items[i]->working), op->key, op->key_size);
    if (entry != NULL) {
      status = praktor_c11_value_store_set(&staged, op->key, entry->value);
      if (status != PRAKTOR_C11_VALUE_OK) {
        praktor_c11_value_store_destroy(&staged); free(items); return status;
      }
    }
  }
  { void *old_impl = target->impl; target->impl = staged.impl; staged.impl = old_impl; }
  praktor_c11_value_store_destroy(&staged);
  for (i = 0u; i < count; ++i) praktor_c11_value_txn_abort(&txns[i]);
  free(items);
  return PRAKTOR_C11_VALUE_OK;
}
