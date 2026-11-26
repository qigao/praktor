/* Compile as a freestanding wasm32 module. */
__attribute__((import_module("praktor"), import_name("input_size")))
int input_size(void);
__attribute__((import_module("praktor"), import_name("input_read")))
int input_read(unsigned offset, char *destination, unsigned length);
__attribute__((import_module("praktor"), import_name("output_write")))
int output_write(const char *source, unsigned length);

static char input[65536];

static unsigned append_uint(char *output, unsigned value) {
    char digits[10];
    unsigned count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    for (unsigned i = 0; i < count; ++i) output[i] = digits[count - 1 - i];
    return count;
}

__attribute__((export_name("run")))
int run(void) {
    int length = input_size();
    if (length < 0 || (unsigned)length > sizeof input ||
        input_read(0, input, (unsigned)length))
        return 1;
    unsigned non_ascii = 0;
    for (int i = 0; i < length; ++i) non_ascii += (unsigned char)input[i] >= 128;
    char output[64];
    static const char first[] = "{\"input_bytes\":";
    static const char second[] = ",\"non_ascii_bytes\":";
    unsigned size = 0;
    for (unsigned i = 0; i < sizeof first - 1; ++i) output[size++] = first[i];
    size += append_uint(output + size, (unsigned)length);
    for (unsigned i = 0; i < sizeof second - 1; ++i) output[size++] = second[i];
    size += append_uint(output + size, non_ascii);
    output[size++] = '}';
    return output_write(output, size);
}
