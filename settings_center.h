#pragma once

#include "settings_center_model.h"

#include <cstdint>
#include <string>

namespace gxos {
namespace apps {

class SettingsCenter {
public:
    static uint64_t Launch(const std::string& route = std::string());
    static int main(int argc, char** argv);
};

} // namespace apps
} // namespace gxos
