#include "../ephemeris_core.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>

struct FileReader {
    std::ifstream file;
    uint64_t sizeLimit;
    bool corrupt;
};

static stellar_native::Context g_context;
static int g_checks = 0;

static void check(bool passed, const char* name) {
    ++g_checks;
    if (!passed) {
        std::cerr << "FAIL: " << name << '\n';
        std::exit(1);
    }
    std::cout << "PASS: " << name << '\n';
}

static bool read_file_at(void* user, uint64_t offset, void* buffer,
                         uint32_t capacity, uint32_t* bytesRead) {
    FileReader* reader = static_cast<FileReader*>(user);
    if (bytesRead) *bytesRead = 0;
    if (!reader || !buffer || !bytesRead || capacity == 0u) return false;
    if (offset >= reader->sizeLimit) return true;
    uint64_t available = reader->sizeLimit - offset;
    uint32_t requested = available < capacity ? static_cast<uint32_t>(available) : capacity;
    reader->file.clear();
    reader->file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!reader->file) return false;
    reader->file.read(static_cast<char*>(buffer), static_cast<std::streamsize>(requested));
    *bytesRead = static_cast<uint32_t>(reader->file.gcount());
    if (reader->corrupt && offset <= 4096u && offset + *bytesRead > 4096u)
        static_cast<uint8_t*>(buffer)[4096u - static_cast<uint32_t>(offset)] ^= 0x01u;
    return reader->file.good() || reader->file.eof();
}

static stellar_native::Reader make_reader(FileReader* file, uint64_t size) {
    stellar_native::Reader reader = { read_file_at, file, size };
    return reader;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: ephemeris_core_test <ephemeris_de440s_chebyshev.sn4bin>\n";
        return 2;
    }
    FileReader file = {};
    file.file.open(argv[1], std::ios::binary);
    file.sizeLimit = stellar_native::kDatasetBytes;
    file.corrupt = false;
    check(file.file.is_open(), "open authenticated DE440s dataset");

    stellar_native::Reader reader = make_reader(&file, stellar_native::kDatasetBytes);
    check(stellar_native::initialize(&g_context, &reader) == stellar_native::Error::Ok,
        "authenticate and initialize bounded dataset index");
    check(g_context.segmentCount == 13u, "validate all 13 indexed segments");

    stellar_native::Vector3 position = {};
    check(stellar_native::evaluatePosition(&g_context, 399, 0.0, &position) == stellar_native::Error::Ok,
        "evaluate Earth at fixed ET 0");
    check(std::fabs(position.x - (-27566740.483)) <= 0.0005 &&
          std::fabs(position.y - 132361381.154) <= 0.0005 &&
          std::fabs(position.z - 57418653.286) <= 0.0005,
        "match hosted SN4 Earth position at ET 0 within 0.001 km display precision");

    stellar_native::Vector3 repeated = {};
    check(stellar_native::evaluatePosition(&g_context, 399, 0.0, &repeated) == stellar_native::Error::Ok &&
          repeated.x == position.x && repeated.y == position.y && repeated.z == position.z,
        "repeat fixed-epoch evaluation deterministically");
    check(stellar_native::evaluatePosition(&g_context, 399,
          std::numeric_limits<double>::quiet_NaN(), &repeated) == stellar_native::Error::InvalidEpoch,
        "reject NaN ET");
    check(stellar_native::evaluatePosition(&g_context, 399,
          std::numeric_limits<double>::infinity(), &repeated) == stellar_native::Error::InvalidEpoch,
        "reject infinite ET");
    check(stellar_native::evaluatePosition(&g_context, 999, 0.0, &repeated) == stellar_native::Error::BodyUnavailable,
        "reject unsupported target ID");
    check(stellar_native::evaluatePosition(&g_context, 399, 5000000000.0, &repeated) ==
          stellar_native::Error::EpochOutOfCoverage,
        "reject out-of-coverage ET without extrapolation");

    stellar_native::shutdown(&g_context);
    check(!g_context.initialized &&
          stellar_native::evaluatePosition(&g_context, 399, 0.0, &repeated) ==
              stellar_native::Error::NotInitialized,
        "shutdown releases state and blocks further evaluation");

    reader = make_reader(&file, stellar_native::kDatasetBytes - 1u);
    check(stellar_native::initialize(&g_context, &reader) == stellar_native::Error::DatasetSizeMismatch,
        "reject an undersized dataset before parsing");

    file.sizeLimit = stellar_native::kDatasetBytes;
    file.corrupt = true;
    reader = make_reader(&file, stellar_native::kDatasetBytes);
    check(stellar_native::initialize(&g_context, &reader) == stellar_native::Error::DatasetHashMismatch,
        "reject modified ephemeris data by trusted SHA-256");

    file.corrupt = false;
    file.sizeLimit = stellar_native::kDatasetBytes - 1u;
    reader = make_reader(&file, stellar_native::kDatasetBytes);
    check(stellar_native::initialize(&g_context, &reader) == stellar_native::Error::ReadFailure,
        "reject a short read at the dataset boundary");

    file.sizeLimit = stellar_native::kDatasetBytes;
    reader = make_reader(&file, stellar_native::kDatasetBytes);
    check(stellar_native::initialize(&g_context, &reader) == stellar_native::Error::Ok,
        "reinitialize successfully after teardown and failed loads");
    stellar_native::shutdown(&g_context);
    file.file.close();

    std::cout << "Native ephemeris tests: " << g_checks << " passed, 0 failed.\n";
    return 0;
}
