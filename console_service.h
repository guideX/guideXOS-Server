#pragma once
#include <string>
#include <functional>
#include <unordered_map>
#include "ipc_bus.h"

namespace gxos { namespace svc {
    class ConsoleService {
    public:
        static uint64_t start(); // spawns the service process
    private:
        static int main(int argc, char** argv);
    };
} }
