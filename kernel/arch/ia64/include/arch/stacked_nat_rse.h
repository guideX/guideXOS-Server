#pragma once

#include <stddef.h>
#include <kernel/types.h>

namespace kernel {
namespace arch {
namespace ia64 {

static const uint64_t STACKED_NAT_RSE_MAGIC = 0x53544e4154525345ull;
static const uint64_t STACKED_NAT_RSE_RETURN_MARKER = 0x53544e4154524554ull;
static const uint64_t STACKED_NAT_RSE_RECORD_SIZE = 320;
static const uint64_t STACKED_NAT_RSE_SENTINEL = 0x4758534f534e4154ull;
static const uint64_t STACKED_NAT_RSE_PARENT_SOF = 3;
static const uint64_t STACKED_NAT_RSE_CHILD_SOF = 96;

struct alignas(16) StackedNatRseRecord {
    uint64_t magic;                       // 0
    uint64_t version;                     // 8
    uint64_t size;                        // 16
    uint64_t sequence;                    // 24
    uint64_t result;                      // 32
    uint64_t static_nat_before;            // 40; checked with tnat
    uint64_t stacked_nat_before;           // 48; GR33, checked with tnat
    uint64_t stacked_nat_after;            // 56; GR33 after child br.ret
    uint64_t static_nat_after;             // 64; GR8 after child call
    uint64_t ordinary_before;              // 72; GR32
    uint64_t ordinary_after;               // 80; GR32 after child br.ret
    uint64_t original_rsc;                 // 88
    uint64_t lazy_rsc;                     // 96; mode sampled after the call
    uint64_t child_rsc;                    // 104; before restricted BSPSTORE/RNAT reads
    uint64_t parent_sof;                   // 112; exact alloc operand result
    uint64_t child_sof;                    // 120; exact alloc operand result
    uint64_t parent_bsp;                   // 128; GR32 spill address
    uint64_t parent_bspstore;              // 136; after flushrs
    uint64_t child_bsp;                    // 144; while child frame is active
    uint64_t child_bspstore;               // 152; after child alloc spill
    uint64_t child_rnat;                   // 160; raw, valid bits are derived
    uint64_t spilled_sentinel;             // 168; read-only observation at parent_bsp
    uint64_t return_bsp;                   // 176; after child br.ret fill
    uint64_t return_bspstore;              // 184; after child br.ret fill
    uint64_t restored_rsc;                 // 192
    uint64_t parent_pfs;                   // 200; saved by parent alloc
    uint64_t child_pfs;                    // 208; saved by child alloc
    uint64_t nat_slot_address;             // 216; GR33 backing-store address
    uint64_t nat_collection_address;       // 224; only read if fully stored
    uint64_t nat_slot_index;               // 232; collection bit for GR33
    uint64_t rnat_source;                  // 240; 1=valid partial AR.RNAT, 2=stored collection
    uint64_t rnat_collection_value;        // 248; only defined when source=2
    uint64_t rnat_observed;                // 256; selected bit only
    uint64_t saved_pr;                     // 264
    uint64_t stage;                        // 272; ordered progress marker
    uint64_t return_marker;                // 280
    uint64_t saved_r12;                    // 288
    uint64_t saved_r13;                    // 296
    uint64_t reserved0;                    // 304
    uint64_t reserved1;                    // 312
};

static_assert(sizeof(StackedNatRseRecord) == STACKED_NAT_RSE_RECORD_SIZE,
              "stacked NaT/RSE record size changed");
static_assert(offsetof(StackedNatRseRecord, result) == 32,
              "stacked NaT/RSE result offset changed");
static_assert(offsetof(StackedNatRseRecord, original_rsc) == 88,
              "stacked NaT/RSE RSC offset changed");
static_assert(offsetof(StackedNatRseRecord, parent_bsp) == 128,
              "stacked NaT/RSE BSP offset changed");
static_assert(offsetof(StackedNatRseRecord, child_rnat) == 160,
              "stacked NaT/RSE RNAT offset changed");
static_assert(offsetof(StackedNatRseRecord, nat_slot_address) == 216,
              "stacked NaT/RSE NaT-slot offset changed");
static_assert(offsetof(StackedNatRseRecord, rnat_source) == 240,
              "stacked NaT/RSE RNAT-source offset changed");
static_assert(offsetof(StackedNatRseRecord, stage) == 272,
              "stacked NaT/RSE stage offset changed");

extern "C" volatile StackedNatRseRecord ia64_stacked_nat_rse_record;
extern "C" void ia64_stacked_nat_rse_test();
extern "C" char rse_backing_store_base[];
extern "C" char rse_backing_store_top[];

bool stacked_nat_rse_record_is_zero();
uint64_t validate_stacked_nat_rse_record(const volatile StackedNatRseRecord& record);
uint64_t stacked_nat_rse_negative_contract_tests(
    const volatile StackedNatRseRecord& record);
void report_stacked_nat_rse_record(const volatile StackedNatRseRecord& record);
bool run_stacked_nat_rse_qualification();

} // namespace ia64
} // namespace arch
} // namespace kernel
