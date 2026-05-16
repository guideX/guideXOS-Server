#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gxos {
namespace apps {

struct ElfValidationResult {
    bool valid = false;
    std::string architecture;
    std::string entryPoint;
    std::vector<std::string> errors;
};

class ElfValidator {
public:
    static ElfValidationResult Validate(const std::vector<uint8_t>& bytes, const std::string& expectedArchitecture);
};

} // namespace apps
} // namespace gxos
