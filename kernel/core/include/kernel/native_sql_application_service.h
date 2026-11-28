#pragma once

#include <stddef.h>
#include <stdint.h>

#include "sdk/include/guidexos/abi.h"

namespace kernel {
namespace native_sql_application {

typedef int (*BufferValidator)(gx_app_context* context, const void* address,
                               size_t length, uint8_t writable);

struct DetachResult {
    uint32_t sessionsBefore;
    uint32_t sessionsReleased;
    uint32_t sessionsAfter;
    gx_sql_database_handle lastReleasedHandle;
    uint8_t principalFound;
    uint8_t success;
};

// Attach one exact Native ELF host context to a bounded, manifest-authorized
// SQL principal. The context address is the owner key; no kernel pointer or
// numeric owner token is exposed to the application.
bool attach(gx_app_context* context, uint64_t ownerId,
            uint8_t databaseReadGranted, BufferValidator validator);

// Detaching closes every SQL session owned by this app context.
// Detach consumes every session owned by this context and reports counts from
// the production session table so the caller can fail closed on bad cleanup.
DetachResult detach(gx_app_context* context);

// Installs the public SDK callbacks into the append-only host-call table.
void installHostCalls(gx_host_calls* host);

} // namespace native_sql_application
} // namespace kernel
