#include "output_scan.h"
#include <string.h>
#include <tinytest.h>

spec("C output scanner") {
  it("scans binary lines and pairs without C-string truncation") {
    const char input[] = "a\0b\n\nlast\r\n";
    praktor_output_span source = {input, sizeof(input) - 1u}, line;
    praktor_output_pair pair;
    size_t offset = 0u;
    check(praktor_output_next_line(source, &offset, &line) == 1 && line.size == 3u && memcmp(line.data, "a\0b", 3u) == 0);
    check(praktor_output_next_line(source, &offset, &line) == 1 && line.size == 0u);
    check(praktor_output_next_line(source, &offset, &line) == 1 && line.size == 5u && memcmp(line.data, "last\r", 5u) == 0);
    check(praktor_output_next_line(source, &offset, &line) == 0);
    check(praktor_output_contains(source, (praktor_output_span){"\0b", 2u}) == 1);
    check(praktor_output_contains(source, (praktor_output_span){"", 0u}) == 1);
    check(praktor_output_contains(source, (praktor_output_span){"missing", 7u}) == 0);
    source = (praktor_output_span){" \tk\0x\r:: \vv\t||skip||=::last||",
        sizeof(" \tk\0x\r:: \vv\t||skip||=::last||") - 1u};
    offset = 0u;
    check(praktor_output_next_pair(source, (praktor_output_span){"::", 2u}, (praktor_output_span){"||", 2u}, &offset, &pair) == 1);
    check(pair.key.size == 3u && memcmp(pair.key.data, "k\0x", 3u) == 0);
    check(pair.value.size == 2u && memcmp(pair.value.data, "\vv", 2u) == 0);
    check(praktor_output_next_pair(source, (praktor_output_span){"::", 2u}, (praktor_output_span){"||", 2u}, &offset, &pair) == 1);
    check(pair.key.size == 1u && pair.key.data[0] == '=' && pair.value.size == 4u);
    check(praktor_output_next_pair(source, (praktor_output_span){"::", 2u}, (praktor_output_span){"||", 2u}, &offset, &pair) == 0);
    offset = 0u;
    check(praktor_output_next_pair(source, (praktor_output_span){"=", 1u}, (praktor_output_span){"", 0u}, &offset, &pair) == -1);
  }
  it("preflights workspace and terminates JSON at exact output bounds") {
    const praktor_output_span input = {"z=first\na=two\nz=last", sizeof("z=first\na=two\nz=last") - 1u};
    const praktor_output_span delimiter = {"=", 1u}, separator = {"\n", 1u};
    const char expected[] = "{\"a\":\"two\",\"z\":\"last\"}";
    praktor_output_pair pairs[2]; char output[sizeof(expected)];
    check(praktor_output_key_value_json(input, delimiter, separator, pairs, 1u, output, sizeof(output)) == -2);
    check(praktor_output_key_value_json(input, delimiter, separator, pairs, 2u, output, sizeof(output) - 1u) == -2);
    check(praktor_output_key_value_json(input, delimiter, separator, pairs, 2u, output, sizeof(output)) == 0);
    check(strcmp(output, expected) == 0);
    check(praktor_output_key_value_json((praktor_output_span){"x=\0", 3u}, delimiter, separator, pairs, 2u, output, sizeof(output)) == -1);
    check(praktor_output_key_value_json((praktor_output_span){NULL, 0u}, delimiter, separator, NULL, 0u, output, 3u) == 0 && strcmp(output, "{}") == 0);
  }
}
