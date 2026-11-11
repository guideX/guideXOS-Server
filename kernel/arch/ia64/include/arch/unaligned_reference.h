#pragma once

#include <stddef.h>
#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace ia64 {

static const uint64_t UNALIGNED_REFERENCE_SCHEMA_VERSION = 1;
static const uint64_t UNALIGNED_REFERENCE_RECORD_SIZE = 384;
static const uint64_t UNALIGNED_REFERENCE_VALID_MARKER = 0xBEEFC0DE55415631ull;
static const uint64_t UNALIGNED_REFERENCE_HANDLER_VALID = 0xBEEFC0DE55414831ull;
static const uint64_t UNALIGNED_REFERENCE_COMPLETION = 0xBEEFC0DE55434331ull;
static const uint64_t UNALIGNED_REFERENCE_RETURN = 0xBEEFC0DE55455231ull;
static const uint64_t UNALIGNED_REFERENCE_CLEANUP = 0xBEEFC0DE55434C31ull;
static const uint64_t UNALIGNED_REFERENCE_POST_RFI_VALID = 0xBEEFC0DE55505631ull;
static const uint64_t UNALIGNED_REFERENCE_DATA_FAULT = 0;

struct alignas(16) UnalignedReferenceRecord {
    uint64_t valid_marker;             // 0
    uint64_t schema_version;            // 8
    uint64_t record_size;               // 16
    uint64_t sequence;                  // 24; claimed at handler entry
    uint64_t vector_offset;             // 32
    uint64_t fault_subtype;             // 40
    uint64_t fault_site;                // 48
    uint64_t saved_iip;                 // 56
    uint64_t saved_slot;                // 64
    uint64_t saved_ipsr;                // 72
    uint64_t saved_ifs;                 // 80; non-valid bits are undefined by the ISA
    uint64_t saved_ifs_valid;           // 88
    uint64_t saved_ifa;                 // 96
    uint64_t saved_isr;                 // 104
    uint64_t isr_ed;                    // 112
    uint64_t isr_ei;                    // 120
    uint64_t isr_ni;                    // 128
    uint64_t isr_sp;                    // 136
    uint64_t isr_read;                  // 144
    uint64_t isr_write;                 // 152
    uint64_t handler_entry_psr;         // 160
    uint64_t handler_entry_rsc;         // 168
    uint64_t handler_entry_bsp;         // 176
    uint64_t saved_predicates;          // 184
    uint64_t pre_fault_psr;             // 192
    uint64_t pre_fault_rsc;             // 200
    uint64_t pre_fault_bsp;             // 208
    uint64_t pre_fault_unat;            // 216
    uint64_t test_address;              // 224
    uint64_t buffer_base;               // 232
    uint64_t buffer_size;               // 240
    uint64_t access_width;              // 248
    uint64_t return_target;             // 256
    uint64_t return_ipsr;               // 264; AC is cleared for the exact continuation
    uint64_t handler_entry_status;      // 272
    uint64_t completion_marker;         // 280
    uint64_t continuation_count;        // 288
    uint64_t return_marker;             // 296
    uint64_t post_rfi_psr;              // 304; sampled by the first continuation instruction
    uint64_t post_rfi_rsc;              // 312
    uint64_t post_rfi_bsp;              // 320
    uint64_t post_rfi_iva;              // 328
    uint64_t post_rfi_unat;             // 336
    uint64_t pre_fault_gr_checksum;     // 344
    uint64_t post_rfi_gr_checksum;      // 352
    uint64_t cleanup_marker;            // 360; AC cleanup serialized before later data access
    uint64_t post_rfi_valid_marker;     // 368; written last
    uint64_t reserved;                  // 376
};

static_assert(sizeof(UnalignedReferenceRecord) == UNALIGNED_REFERENCE_RECORD_SIZE,
              "unaligned reference record layout changed");
static_assert(offsetof(UnalignedReferenceRecord, saved_iip) == 56,
              "unaligned reference IIP offset changed");
static_assert(offsetof(UnalignedReferenceRecord, saved_ifa) == 96,
              "unaligned reference IFA offset changed");
static_assert(offsetof(UnalignedReferenceRecord, saved_isr) == 104,
              "unaligned reference ISR offset changed");
static_assert(offsetof(UnalignedReferenceRecord, return_ipsr) == 264,
              "unaligned reference return IPSR offset changed");
static_assert(offsetof(UnalignedReferenceRecord, post_rfi_psr) == 304,
              "unaligned reference post-RFI PSR offset changed");
static_assert(offsetof(UnalignedReferenceRecord, post_rfi_valid_marker) == 368,
              "unaligned reference post-RFI validity offset changed");

extern "C" volatile UnalignedReferenceRecord ia64_unaligned_reference_record;
extern "C" char ia64_unaligned_reference_buffer[];
extern "C" char ia64_unaligned_reference_fault_site[];
extern "C" char ia64_unaligned_reference_continuation[];
extern "C" char ia64_ivt[];
extern "C" char rse_backing_store_base[];
extern "C" char rse_backing_store_top[];

extern "C" int ia64_unaligned_reference_test();
bool run_unaligned_reference_qualification();

} // namespace ia64
} // namespace arch
} // namespace kernel
