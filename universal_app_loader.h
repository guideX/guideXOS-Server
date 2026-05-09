#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gxos {

class UniversalAppLoader {
public:
    static bool Execute(const std::string& packagePath, std::string& error);
    static bool LoadBinary(const std::string& packagePath, std::vector<uint8_t>& binary, std::string& entryPoint, std::string& error);
};

} // namespace gxos

using UniversalAppLoader = gxos::UniversalAppLoader;
