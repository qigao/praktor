#include "file_hash.h"
#include <stdio.h>
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
    it("streams files and preserves output on failure") {
        static const char path[] = "praktor_file_hash_fixture.bin";
        char output[PRAKTOR_FILE_HASH_HEX_SIZE] = "unchanged";
        FILE *file;
        (void)remove(path);
        file = fopen(path, "wb");
        check(file != NULL);
        if (file != NULL) {
            check(fwrite("hello", 1u, 5u, file) == 5u);
            check(fclose(file) == 0);
        }
        check(praktor_file_hash_file(path, output, sizeof(output)) == PRAKTOR_FILE_HASH_OK);
        check(strcmp(output, "a430d84680aabd0b") == 0);
        strcpy(output, "unchanged");
        check(praktor_file_hash_file("praktor_file_hash_missing.bin", output, sizeof(output)) != PRAKTOR_FILE_HASH_OK);
        check(strcmp(output, "unchanged") == 0);
        strcpy(output, "unchanged");
        check(praktor_file_hash_bytes(NULL, 1u, output, sizeof(output)) != PRAKTOR_FILE_HASH_OK);
        check(strcmp(output, "unchanged") == 0);
        (void)remove(path);
    }
}
