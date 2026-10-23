#include "output_scan.h"
#include <string.h>

static int valid(praktor_output_span span) { return span.data != NULL || span.size == 0u; }
static size_t find(praktor_output_span input, praktor_output_span search) {
  size_t i;
  if (search.size > input.size) return input.size;
  if (search.size == 0u) return 0u;
  for (i = 0u; i <= input.size - search.size; ++i)
    if (memcmp(input.data + i, search.data, search.size) == 0) return i;
  return input.size;
}
int praktor_output_contains(praktor_output_span input, praktor_output_span search) {
  if (!valid(input) || !valid(search)) return 0;
  return search.size == 0u || find(input, search) < input.size;
}
int praktor_output_next_line(praktor_output_span input, size_t *offset,
                             praktor_output_span *line) {
  size_t start, end;
  if (!valid(input) || offset == NULL || line == NULL || *offset > input.size) return -1;
  start = *offset;
  if (start == input.size) return 0;
  for (end = start; end < input.size && input.data[end] != '\n'; ++end) {}
  line->data = input.data + start; line->size = end - start;
  *offset = end < input.size ? end + 1u : end;
  return 1;
}
static int whitespace(char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; }
static praktor_output_span trim(praktor_output_span span) {
  while (span.size != 0u && whitespace(*span.data)) { ++span.data; --span.size; }
  while (span.size != 0u && whitespace(span.data[span.size - 1u])) --span.size;
  return span;
}
int praktor_output_next_pair(praktor_output_span input, praktor_output_span delimiter,
                             praktor_output_span separator, size_t *offset,
                             praktor_output_pair *pair) {
  if (!valid(input) || !valid(delimiter) || !valid(separator) || separator.size == 0u ||
      offset == NULL || pair == NULL || *offset > input.size) return -1;
  while (*offset < input.size) {
    praktor_output_span line = {input.data + *offset, input.size - *offset};
    size_t length = find(line, separator), split;
    *offset += length;
    if (length < line.size) *offset += separator.size;
    line.size = length;
    split = find(line, delimiter);
    if (delimiter.size == 0u || split < line.size) {
      pair->key = line; pair->key.size = split;
      pair->value.data = line.data + split + delimiter.size;
      pair->value.size = line.size - split - delimiter.size;
      pair->key = trim(pair->key); pair->value = trim(pair->value);
      return 1;
    }
  }
  return 0;
}
static int compare(praktor_output_span left, praktor_output_span right) {
  size_t size = left.size < right.size ? left.size : right.size;
  int order = size == 0u ? 0 : memcmp(left.data, right.data, size);
  if (order != 0) return order;
  return left.size < right.size ? -1 : left.size > right.size;
}
typedef struct writer { char *output; size_t capacity, size; } writer;
static int append(writer *out, const char *text, size_t size) {
  if (size >= out->capacity - out->size) return -2;
  memcpy(out->output + out->size, text, size); out->size += size;
  out->output[out->size] = '\0'; return 0;
}
static int string(writer *out, praktor_output_span span) {
  static const char hex[] = "0123456789abcdef";
  size_t i;
  if (append(out, "\"", 1u) != 0) return -2;
  for (i = 0u; i < span.size; ++i) {
    unsigned char ch = (unsigned char)span.data[i];
    const char *escaped = NULL;
    switch (ch) {
      case '"': escaped = "\\\""; break;
      case '\\': escaped = "\\\\"; break;
      case '\b': escaped = "\\b"; break;
      case '\f': escaped = "\\f"; break;
      case '\n': escaped = "\\n"; break;
      case '\r': escaped = "\\r"; break;
      case '\t': escaped = "\\t"; break;
      default: break;
    }
    if (escaped != NULL) { if (append(out, escaped, 2u) != 0) return -2; }
    else if (ch < 32u) {
      char code[] = {'\\', 'u', '0', '0', hex[ch >> 4u], hex[ch & 15u]};
      if (append(out, code, sizeof(code)) != 0) return -2;
    } else if (append(out, span.data + i, 1u) != 0) return -2;
  }
  return append(out, "\"", 1u);
}
int praktor_output_key_value_json(praktor_output_span input,
    praktor_output_span delimiter, praktor_output_span separator,
    praktor_output_pair *pairs, size_t pair_capacity, char *output, size_t output_capacity) {
  size_t offset = 0u, count = 0u, i, index;
  praktor_output_pair pair;
  writer out = {output, output_capacity, 0u};
  int result;
  if (!valid(input) || !valid(delimiter) || !valid(separator) || separator.size == 0u ||
      output == NULL || output_capacity == 0u || (pair_capacity != 0u && pairs == NULL) ||
      (input.size != 0u && memchr(input.data, '\0', input.size) != NULL) ||
      (delimiter.size != 0u && memchr(delimiter.data, '\0', delimiter.size) != NULL) ||
      memchr(separator.data, '\0', separator.size) != NULL) return -1;
  while ((result = praktor_output_next_pair(input, delimiter, separator, &offset, &pair)) > 0) {
    /* Maintain sorted unique keys without qsort's possible heap allocation. */
    for (index = 0u; index < count && compare(pairs[index].key, pair.key) < 0; ++index) {}
    if (index < count && compare(pairs[index].key, pair.key) == 0) pairs[index] = pair;
    else {
      if (count == pair_capacity) return -2;
      for (i = count; i > index; --i) pairs[i] = pairs[i - 1u];
      pairs[index] = pair; ++count;
    }
  }
  if (result < 0) return result;
  if (append(&out, "{", 1u) != 0) return -2;
  for (i = 0u; i < count; ++i) {
    if ((i != 0u && append(&out, ",", 1u) != 0) || string(&out, pairs[i].key) != 0 ||
        append(&out, ":", 1u) != 0 || string(&out, pairs[i].value) != 0) return -2;
  }
  return append(&out, "}", 1u);
}
