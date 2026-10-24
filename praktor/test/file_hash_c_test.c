#include "file_hash.h"
#include <string.h>
#include <tinytest.h>

spec("C file hashing") {
    it("matches stable FNV-1a vectors") {
        char output[PRAKTOR_FILE_HASH_HEX_SIZE] = {0};
        check(praktor_file_hash_bytes(NULL, 0u, output, sizeof(output)) == PRAKTOR_FILE_HASH_OK);
        check(strcmp(output, "cbf29ce484222325") == 0);
        check(praktor_file_hash_bytes("hello", 5u, output, sizeof(output)) == PRAKTOR_FILE_HASH_OK);
        check(strcmp(output, "a430d84680aabd0b") == 0);
    }
    it("rejects invalid buffers without modifying output") {
        char output[PRAKTOR_FILE_HASH_HEX_SIZE] = "unchanged";
        check(praktor_file_hash_bytes(NULL, 1u, output, sizeof(output)) != PRAKTOR_FILE_HASH_OK);
        check(strcmp(output, "unchanged") == 0);
        check(praktor_file_hash_bytes("hello", 5u, output, sizeof(output) - 1u) != PRAKTOR_FILE_HASH_OK);
        check(strcmp(output, "unchanged") == 0);
    }
}
