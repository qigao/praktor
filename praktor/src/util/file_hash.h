#ifndef PRAKTOR_FILE_HASH_H
#define PRAKTOR_FILE_HASH_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define PRAKTOR_FILE_HASH_HEX_SIZE 17u
enum { PRAKTOR_FILE_HASH_OK = 0, PRAKTOR_FILE_HASH_ERROR = -1 };

/* FNV-1a 64-bit hashes in stable, lowercase 16-hex-digit form. Output is
 * untouched on invalid arguments, short capacity, open/read/close failure.
 * The file API streams input and does not retain the file contents. */
int praktor_file_hash_bytes(const void *data, size_t size,
                            char *out, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
