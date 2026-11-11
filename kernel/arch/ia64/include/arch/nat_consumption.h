#pragma once

#include <stddef.h>
#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace ia64 {

static const uint64_t NAT_CONSUMPTION_SCHEMA_VERSION = 1;
static const uint64_t NAT_CONSUMPTION_RECORD_SIZE = 512;
static const uint64_t NAT_CONSUMPTION_REGISTER_FAULT = 43;
static const uint64_t NAT_CONSUMPTION_VALID_MARKER = 0xBEEFC0DE4E415631ull;
static const uint64_t NAT_CONSUMPTION_HANDLER_VALID = 0xBEEFC0DE4E414831ull;
static const uint64_t NAT_CONSUMPTION_COMPLETION = 0xBEEFC0DE4E414331ull;
static const uint64_t NAT_CONSUMPTION_RETURN = 0xBEEFC0DE4E415231ull;
static const uint64_t NAT_CONSUMPTION_POST_RFI_VALID = 0xBEEFC0DE4E415031ull;

struct alignas(16) NatConsumptionRecord {
    uint64_t valid_marker;             // 0
    uint64_t schema_version;            // 8
    uint64_t record_size;               // 16
    uint64_t sequence;                  // 24
    uint64_t vector_offset;             // 32
    uint64_t fault_subtype;             // 40; architectural interruption number 43
    uint64_t fault_site;                // 48
    uint64_t saved_iip;                 // 56
    uint64_t saved_slot;                // 64
    uint64_t saved_ipsr;                // 72
    uint64_t saved_iipa;                // 80; captured, diagnostic only
    uint64_t saved_ifs;                 // 88; only bit 63 is defined here
    uint64_t saved_ifs_valid;           // 96
    uint64_t saved_ifa;                 // 104; captured but not architecturally valid for this subtype
    uint64_t ifa_architecturally_valid; // 112; always zero for this contract
    uint64_t saved_isr;                 // 120
    uint64_t isr_ed;                    // 128
    uint64_t isr_ei;                    // 136
    uint64_t isr_so;                    // 144
    uint64_t isr_ni;                    // 152
    uint64_t isr_ir;                    // 160
    uint64_t isr_rs;                    // 168
    uint64_t isr_sp;                    // 176
    uint64_t isr_na;                    // 184
    uint64_t isr_read;                  // 192
    uint64_t isr_write;                 // 200
    uint64_t isr_execute;               // 208
    uint64_t saved_iib0;                // 216; zero when the simulator does not expose it
    uint64_t saved_iib1;                // 224
    uint64_t handler_entry_psr;         // 232
    uint64_t handler_entry_rsc;         // 240
    uint64_t handler_entry_bsp;         // 248
    uint64_t saved_predicates;          // 256
    uint64_t source_register;           // 264; caller-saved static GR8
    uint64_t destination_register;      // 272; GR9, result intentionally unused
    uint64_t speculative_load_site;     // 280
    uint64_t pre_fault_psr;             // 288
    uint64_t speculative_load_psr;      // 296; PSR.ed must be set
    uint64_t pre_fault_rsc;             // 304
    uint64_t pre_fault_bsp;             // 312
    uint64_t pre_fault_unat;            // 320; observed, not modified by this contract
    uint64_t source_nat_before_fault;   // 328; tnat result
    uint64_t source_nat_at_handler;     // 336; tnat result
    uint64_t source_nat_after_rfi;      // 344; tnat result before clearing GR8
    uint64_t source_nat_after_clear;    // 352; tnat result after mov r8=r0
    uint64_t return_target;             // 360
    uint64_t return_ipsr;               // 368
    uint64_t handler_status;            // 376
    uint64_t completion_marker;         // 384
    uint64_t continuation_count;        // 392
    uint64_t return_marker;             // 400
    uint64_t post_rfi_psr;              // 408; first continuation instruction samples this
    uint64_t post_rfi_rsc;              // 416
    uint64_t post_rfi_bsp;              // 424
    uint64_t post_rfi_unat;             // 432
    uint64_t pre_fault_gr_checksum;     // 440; excludes the NaTed GR8
    uint64_t post_rfi_gr_checksum;      // 448
    uint64_t test_result;               // 456
    uint64_t post_rfi_valid_marker;     // 464; written last
    uint64_t saved_iib0_available;      // 472; Ski 1.5.1 does not model CR.IIB0
    uint64_t saved_iib1_available;      // 480; Ski 1.5.1 does not model CR.IIB1
    uint64_t source_initial_value;      // 488; known non-NaT target address before ld8.s
    uint64_t post_rfi_iva;              // 496
    uint64_t post_speculative_load_psr; // 504; sampled after the forced restart load
};

static_assert(sizeof(NatConsumptionRecord) == NAT_CONSUMPTION_RECORD_SIZE,
              "NaT Consumption record layout changed");
static_assert(offsetof(NatConsumptionRecord, saved_iip) == 56,
              "NaT Consumption IIP offset changed");
static_assert(offsetof(NatConsumptionRecord, saved_isr) == 120,
              "NaT Consumption ISR offset changed");
static_assert(offsetof(NatConsumptionRecord, source_nat_before_fault) == 328,
              "NaT Consumption source-state offset changed");
static_assert(offsetof(NatConsumptionRecord, post_rfi_psr) == 408,
              "NaT Consumption post-RFI PSR offset changed");
static_assert(offsetof(NatConsumptionRecord, post_rfi_valid_marker) == 464,
              "NaT Consumption validity offset changed");

extern "C" volatile NatConsumptionRecord ia64_nat_consumption_record;
extern "C" char ia64_nat_consumption_fault_site[];
extern "C" char ia64_nat_consumption_continuation[];
extern "C" char ia64_nat_consumption_spec_load_site[];
extern "C" char ia64_nat_consumption_pointer[];
extern "C" char ia64_nat_consumption_target[];
extern "C" char ia64_ivt[];
extern "C" char rse_backing_store_base[];
extern "C" char rse_backing_store_top[];

extern "C" int ia64_nat_consumption_test();
bool run_nat_consumption_qualification();

} // namespace ia64
} // namespace arch
} // namespace kernel
