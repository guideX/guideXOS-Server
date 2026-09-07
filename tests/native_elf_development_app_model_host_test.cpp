// Host coverage for the bounded, in-memory NativeElf development registration.

#include "../kernel/core/native_elf/native_elf_development_app_model.h"

#include <cstdio>

using namespace kernel::native_elf::NativeElfDevelopmentAppModel;

static RegistrationRequest valid_request(uint64_t handle = 7)
{
    RegistrationRequest request = {};
    request.handle = handle;
    request.generation = handle;
    request.applicationId = "dev.guidexos.phase27w";
    request.displayName = "Phase 27W Run";
    request.projectRoot = "/P27W";
    request.artifactPath = "build/bin/amd64/p27w.elf";
    request.artifactSize = 128;
    request.artifactSha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    return request;
}

static bool require(bool condition, const char* message)
{
    if (!condition) std::fprintf(stderr, "native_elf_development_app_model_host_test: %s\n", message);
    return condition;
}

int main()
{
    Registration registration = {};
    RegistrationRequest request = valid_request();
    if (!require(register_temporary(request, &registration) == RegistrationResult::Registered,
                 "valid registration rejected") ||
        !require(has_active_registration(), "registration was not retained") ||
        !require(registration.handle == request.handle && registration.artifactSize == request.artifactSize,
                 "registration identity was not retained") ||
        !require(resolve_temporary(request.handle, request.generation, request.applicationId, &registration),
                 "exact registration did not resolve") ||
        !require(register_temporary(request, nullptr) == RegistrationResult::DeploymentAlreadyActive,
                 "duplicate registration was accepted") ||
        !require(!resolve_temporary(request.handle + 1, request.generation, request.applicationId, &registration),
                 "stale handle resolved") ||
        !require(unregister_temporary(request.handle, request.generation, request.applicationId),
                 "registration cleanup failed") ||
        !require(!has_active_registration(), "registration survived cleanup")) return 1;

    const RegistrationRequest invalidRoot = [] {
        RegistrationRequest value = valid_request(8);
        value.projectRoot = "/P27W/../outside";
        return value;
    }();
    if (!require(register_temporary(invalidRoot, nullptr) == RegistrationResult::Invalid,
                 "escaping root was accepted") ||
        !require(!has_active_registration(), "invalid request left registration state")) return 1;

    const RegistrationRequest invalidArtifact = [] {
        RegistrationRequest value = valid_request(9);
        value.artifactPath = "../../outside.elf";
        return value;
    }();
    if (!require(register_temporary(invalidArtifact, nullptr) == RegistrationResult::Invalid,
                 "escaping artifact was accepted") ||
        !require(!has_active_registration(), "invalid artifact left registration state")) return 1;

    const RegistrationRequest externalId = [] {
        RegistrationRequest value = valid_request(10);
        value.applicationId = "com.example.external";
        return value;
    }();
    if (!require(register_temporary(externalId, nullptr) == RegistrationResult::Invalid,
                 "non-development identity was accepted")) return 1;

    std::puts("native_elf_development_app_model_host_test: PASS");
    return 0;
}
