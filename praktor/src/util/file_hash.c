#include "file_hash.h"
#include <cmeta_fs.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint64_t update_hash(uint64_t hash, const unsigned char *data, size_t size) {
    size_t i;
    for (i = 0u; i < size; ++i) {
        hash ^= (uint64_t)data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static int format_hash(uint64_t hash, char *out, size_t capacity) {
    char encoded[PRAKTOR_FILE_HASH_HEX_SIZE];
    int written;
    if (out == NULL || capacity < PRAKTOR_FILE_HASH_HEX_SIZE) return PRAKTOR_FILE_HASH_ERROR;
    written = snprintf(encoded, sizeof(encoded), "%016llx", (unsigned long long)hash);
    if (written != 16) return PRAKTOR_FILE_HASH_ERROR;
    encoded[16] = '\0';
    memcpy(out, encoded, sizeof(encoded));
    return PRAKTOR_FILE_HASH_OK;
}

int praktor_file_hash_bytes(const void *data, size_t size, char *out, size_t capacity) {
    const uint64_t offset_basis = UINT64_C(14695981039346656037);
    uint64_t hash;
    if (out == NULL || capacity < PRAKTOR_FILE_HASH_HEX_SIZE || (data == NULL && size != 0u))
        return PRAKTOR_FILE_HASH_ERROR;
    hash = update_hash(offset_basis, (const unsigned char *)data, size);
    return format_hash(hash, out, capacity);
}

int praktor_file_hash_file(const char *path, char *out, size_t capacity) {
    cmeta_fs_buf_t file = {0};
    char encoded[PRAKTOR_FILE_HASH_HEX_SIZE];
    int status;
    if (path == NULL || path[0] == '\0' || out == NULL ||
        capacity < PRAKTOR_FILE_HASH_HEX_SIZE)
        return PRAKTOR_FILE_HASH_ERROR;
    if (cmeta_fs_read_file(path, &file) != 0) {
        cmeta_fs_buf_free(&file);
        return PRAKTOR_FILE_HASH_ERROR;
    }
    status = praktor_file_hash_bytes(file.base, file.len, encoded, sizeof(encoded));
    cmeta_fs_buf_free(&file);
    if (status != PRAKTOR_FILE_HASH_OK) return PRAKTOR_FILE_HASH_ERROR;
    memcpy(out, encoded, sizeof(encoded));
    return PRAKTOR_FILE_HASH_OK;
}
