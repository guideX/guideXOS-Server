#include "vfs.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <chrono>
#include <string>
#include <vector>

namespace {
int failures = 0;
int checks = 0;

void check(bool condition, const char* name) {
    ++checks;
    if (condition) std::cout << "PASS: " << name << "\n";
    else {
        std::cout << "FAIL: " << name << "\n";
        ++failures;
    }
}
}

int main() {
    const std::filesystem::path root = std::filesystem::current_path() / "tmp" /
        ("vfs-hosted-read-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const std::string lfText = "first line\nsecond line\n";
    const std::string crlfText = "first line\r\nsecond line\r\n";
    const std::string virtualRoot = "/tmp/" + root.filename().generic_string();
    {
        std::ofstream lf(root / "lf.txt", std::ios::binary);
        lf.write(lfText.data(), static_cast<std::streamsize>(lfText.size()));
        std::ofstream crlf(root / "crlf.txt", std::ios::binary);
        crlf.write(crlfText.data(), static_cast<std::streamsize>(crlfText.size()));
    }

    std::vector<uint8_t> bytes;
    check(gxos::Vfs::instance().readFile(virtualRoot + "/lf.txt", bytes) &&
        std::string(bytes.begin(), bytes.end()) == lfText,
        "hosted VFS reads a host-backed LF document through its virtual path");
    check(gxos::Vfs::instance().readFile(virtualRoot + "/crlf.txt", bytes) &&
        std::string(bytes.begin(), bytes.end()) == crlfText,
        "hosted VFS preserves CRLF document bytes");

    const std::vector<uint8_t> memoryBytes = { 'm', 'e', 'm', 'o', 'r', 'y' };
    check(gxos::Vfs::instance().writeFile("/phase6-memory-priority.txt", memoryBytes) &&
        gxos::Vfs::instance().readFile("/phase6-memory-priority.txt", bytes) && bytes == memoryBytes,
        "in-memory VFS entries remain the primary read source");
    check(!gxos::Vfs::instance().readFile(virtualRoot + "/missing.txt", bytes) && bytes.empty(),
        "missing hosted document fails cleanly without retaining the previous read buffer");

    std::filesystem::remove(root / "lf.txt");
    std::filesystem::remove(root / "crlf.txt");
    std::filesystem::remove(root);
    std::cout << "hostedVfsReadChecks=" << (checks - failures) << "/" << checks << "\n";
    return failures == 0 ? 0 : 1;
}
