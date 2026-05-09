#pragma once

#include "cpu_architecture.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gxos {

    struct GXAppBinary {
        CpuArchitecture architecture = CpuArchitecture::Unknown;
        std::string entryPoint;
        std::vector<uint8_t> data;
    };

    struct GXAppMetadata {
        std::string applicationName;
        std::string version;
        std::string requiredGuideXOSVersion;
    };

    class GXApp {
    public:
        static GXApp Open(const std::string& path);
        static GXApp Create(const GXAppMetadata& metadata);

        bool Save(const std::string& path) const;
        bool IsValid() const;
        const std::string& GetLastError() const;

        const GXAppMetadata& GetMetadata() const;
        void SetMetadata(const GXAppMetadata& metadata);

        bool AddBinary(CpuArchitecture architecture, const std::string& entryPoint, const std::vector<uint8_t>& binary);
        bool HasBinary(CpuArchitecture architecture) const;
        std::vector<uint8_t> GetBinary(CpuArchitecture architecture) const;
        bool ExtractBinary(CpuArchitecture architecture, const std::string& outputPath) const;
        std::string GetEntryPoint(CpuArchitecture architecture) const;
        std::vector<CpuArchitecture> GetArchitectures() const;

    private:
        friend class GXAppContainer;

        GXAppMetadata metadata_;
        std::vector<GXAppBinary> binaries_;
        bool valid_ = false;
        std::string lastError_;

        void setError(const std::string& error);
    };

    class GXAppContainer {
    public:
        static GXApp Create(const GXAppMetadata& metadata);
        static GXApp Open(const std::string& path);
        static bool CreatePackage(const std::string& path, const GXAppMetadata& metadata, const std::vector<GXAppBinary>& binaries);
        static bool ExtractBinary(const std::string& packagePath, CpuArchitecture architecture, const std::string& outputPath);
        static const char* ArchitectureToString(CpuArchitecture architecture);
        static CpuArchitecture ArchitectureFromString(const std::string& architecture);
    };

} // namespace gxos

using GXApp = gxos::GXApp;
using GXAppContainer = gxos::GXAppContainer;
using GXAppMetadata = gxos::GXAppMetadata;
using GXAppBinary = gxos::GXAppBinary;
using CpuArchitecture = gxos::CpuArchitecture;
