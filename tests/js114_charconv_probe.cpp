#include <charconv>

extern "C" int js114_charconv_probe(char* output, size_t output_size,
                                    const char* input, size_t input_size) {
    const auto encoded = std::to_chars(output, output + output_size, 1.25,
                                       std::chars_format::general);
    if (encoded.ec != std::errc()) return 1;
    double value = 0.0;
    const auto decoded = std::from_chars(input, input + input_size, value,
                                         std::chars_format::general);
    return decoded.ec == std::errc() ? 0 : 2;
}
