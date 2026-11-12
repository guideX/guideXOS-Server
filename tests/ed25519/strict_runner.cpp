#include <array>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../../kernel/core/include/gxos_ed25519.h"

static bool decode_hex(const std::string &hex, uint8_t *output, size_t capacity,
                       size_t &decoded_size)
{
    decoded_size = 0;
    if (hex == "-") {
        return true;
    }
    if ((hex.size() & 1U) != 0U || hex.size() / 2U > capacity) {
        return false;
    }
    for (size_t i = 0; i < hex.size(); i += 2U) {
        unsigned value = 0;
        std::istringstream parser(hex.substr(i, 2U));
        parser >> std::hex >> value;
        if (!parser || value > 255U) {
            return false;
        }
        output[decoded_size++] = static_cast<uint8_t>(value);
    }
    return true;
}

static bool split_fields(const std::string &line, std::array<std::string, 9> &fields)
{
    std::istringstream parser(line);
    for (size_t i = 0; i < fields.size(); ++i) {
        if (!std::getline(parser, fields[i], '|')) {
            return false;
        }
    }
    return true;
}

int main()
{
    std::string line;
    while (std::getline(std::cin, line)) {
        std::array<std::string, 9> fields;
        std::array<uint8_t, GXOS_ED25519_PUBLIC_KEY_BYTES> public_key{};
        std::array<uint8_t, GXOS_ED25519_SIGNATURE_BYTES> signature{};
        std::vector<uint8_t> message;
        size_t public_key_size = 0;
        size_t signature_size = 0;
        size_t ignored_message_size = 0;
        bool valid = split_fields(line, fields) &&
                     decode_hex(fields[0], public_key.data(), public_key.size(), public_key_size) &&
                     decode_hex(fields[1], signature.data(), signature.size(), signature_size);

        if (valid && fields[2] != "-") {
            if ((fields[2].size() & 1U) != 0U) {
                valid = false;
            } else {
                message.resize(fields[2].size() / 2U);
                valid = decode_hex(fields[2], message.data(), message.size(), ignored_message_size);
            }
        }

        int result = 0;
        if (valid) {
            size_t public_key_len = static_cast<size_t>(std::stoul(fields[3]));
            size_t signature_len = static_cast<size_t>(std::stoul(fields[4]));
            size_t message_len = static_cast<size_t>(std::stoul(fields[5]));
            const uint8_t *public_key_ptr = fields[6] == "1" ? nullptr : public_key.data();
            const uint8_t *signature_ptr = fields[7] == "1" ? nullptr : signature.data();
            const uint8_t *message_ptr = fields[8] == "1" ? nullptr : message.data();
            result = gxos_ed25519_verify_strict(public_key_ptr, public_key_len,
                                                signature_ptr, signature_len,
                                                message_ptr, message_len);
        }
        std::cout << (result == 1 ? "1" : "0") << '\n';
    }
    return 0;
}
