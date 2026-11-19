#pragma once

#include <kernel/types.h>

namespace ia64_coop_probe {

struct alignas(16) FpImage {
    uint64_t low;
    uint64_t high;
};

// Private fixed-size record for the bounded Ski qualification only. 512-byte
// alignment makes GR4-GR7 spill slots and their AR.UNAT bits deterministic.
struct alignas(512) Context {
    uint64_t gr4_to_gr7[4];       // 0..31: st8.spill / ld8.fill slots
    uint64_t initial_gr8;         // 32: first-entry-only NaTed source
    uint64_t sp;                  // 40
    uint64_t gp;                  // 48
    uint64_t tp;                  // 56
    uint64_t rp;                  // 64
    uint64_t pfs;                 // 72
    uint64_t rsc;                 // 80
    uint64_t bsp;                 // 88
    uint64_t bspstore;            // 96
    uint64_t rnat;                // 104
    uint64_t caller_unat;         // 112
    uint64_t spill_unat;          // 120
    uint64_t lc;                  // 128
    uint64_t ec;                  // 136
    uint64_t ccv;                 // 144
    uint64_t pr;                  // 152
    uint64_t br1_to_br5[5];       // 160..199
    uint64_t fpsr;                // 200
    FpImage fp2_to_fp5_and_fp16_to_fp31[20]; // 208..527
    uint64_t stack_base;          // 528
    uint64_t stack_limit;         // 536
    uint64_t rbs_base;            // 544
    uint64_t rbs_limit;           // 552
    uint64_t first_entry;         // 560
    uint64_t context_id;          // 568
    uint64_t first_entries;       // 576
    uint64_t resume_entries;      // 584
    uint64_t switch_count;        // 592
    uint64_t profile_magic;       // 600
    uint64_t stack_canary_low;    // 608
    uint64_t stack_canary_high;   // 616
    uint64_t rbs_canary_low;      // 624
    uint64_t rbs_canary_high;     // 632
    uint64_t rnat_valid_mask;     // 640
    uint64_t rnat_defined_bits;   // 648
    uint64_t observed_sp;         // 656
    uint64_t observed_bsp;        // 664
    uint64_t observed_bspstore;   // 672
    uint64_t observed_psr;        // 680
    uint64_t code_base;            // 688
    uint64_t code_limit;           // 696
    uint64_t reserved[39];         // 704..1015
};

struct Event {
    uint64_t sequence;
    uint64_t context_id;
    uint64_t event_code;
};

struct ProbeRecord {
    uint64_t event_count;
    Event events[8];
    uint64_t errors;
    uint64_t nat_a_before;
    uint64_t nat_a_after;
    uint64_t nat_b_before;
    uint64_t nat_b_after;
    uint64_t psr_a_after_resume;
    uint64_t psr_b_after_resume;
    uint64_t fp_a_low;
    uint64_t fp_a_high;
    uint64_t fp_b_low;
    uint64_t fp_b_high;
    uint64_t pfs_a_first_resume;
    uint64_t pfs_a_final_resume;
    uint64_t pfs_b_first_entry;
    uint64_t pfs_b_resume;
    uint64_t fp_b_first_low;
    uint64_t fp_b_first_high;
    uint64_t a_final_count;
    FpImage fp_a_first_resume[20];
    FpImage fp_a_final_resume[20];
    FpImage fp_b_first_entry[20];
    FpImage fp_b_resume[20];
    uint64_t ec_b_bootstrap;
    uint64_t pfs_b_bootstrap;
    uint64_t ec_a_initial;
    uint64_t ec_a_first_resume;
    uint64_t ec_a_final_resume;
    uint64_t ec_b_first_entry;
    uint64_t ec_b_resume;
    uint64_t failure_seen;
    uint64_t failure_actual;
    uint64_t failure_expected;
    uint64_t failure_sp;
    uint64_t failure_pfs;
    uint64_t failure_ec;
    uint64_t failure_lc;
    uint64_t failure_bsp;
    uint64_t failure_rsc;
    uint64_t failure_gp;
};

static const uint64_t kProfileMagic = 0x3541434f4f505631ULL; // "1VPOCA5"
static const uint64_t kContextA = 0xA;
static const uint64_t kContextB = 0xB;
static const uint64_t kCanaryStackLow = 0x535441434b4c4f57ULL;
static const uint64_t kCanaryStackHigh = 0x535441434b484947ULL;
static const uint64_t kCanaryRbsLow = 0x5242534c4f574341ULL;
static const uint64_t kCanaryRbsHigh = 0x5242534849474843ULL;
static const uint64_t kSentinelA = 0x47584f5341413031ULL;
static const uint64_t kSentinelB = 0x47584f5342423032ULL;
static const uint64_t kR8NaTData = 0x4e41545354415445ULL;
static const uint64_t kFpALow = 0x415f46505f303031ULL;
static const uint64_t kFpAHigh = 0x3fff800000000000ULL;
static const uint64_t kFpBLow = 0x425f46505f303032ULL;
static const uint64_t kFpBHigh = 0x3fff800000000000ULL;
static const uint64_t kExpectedEcA = 0x15;
static const uint64_t kExpectedEcB = 0x2a;
static const uint64_t kExpectedLcA = 0x31;
static const uint64_t kExpectedLcB = 0x42;
static const uint64_t kFpImageStride = 0x100;
static const uint64_t kStackCanaryLow = 0x535441434b4c4f57ULL;
static const uint64_t kStackCanaryHigh = 0x535441434b484947ULL;
static const uint64_t kRbsCanaryLow = 0x5242534c4f574341ULL;
static const uint64_t kRbsCanaryHigh = 0x5242534849474843ULL;
static const uint64_t kPsrInterruptEnable = 0x4000;
static const uint64_t kStackUseALow = 0x415f535441434b30ULL;
static const uint64_t kStackUseAHigh = 0x415f535441434b31ULL;
static const uint64_t kStackUseBLow = 0x425f535441434b30ULL;
static const uint64_t kStackUseBHigh = 0x425f535441434b31ULL;

static_assert(sizeof(FpImage) == 16, "FP save slots must be 16 bytes");
static_assert(offsetof(Context, initial_gr8) == 32, "initial GR8 offset changed");
static_assert(offsetof(Context, sp) == 40, "SP offset changed");
static_assert(offsetof(Context, rp) == 64, "RP offset changed");
static_assert(offsetof(Context, pfs) == 72, "PFS offset changed");
static_assert(offsetof(Context, rsc) == 80, "RSC offset changed");
static_assert(offsetof(Context, bsp) == 88, "BSP offset changed");
static_assert(offsetof(Context, bspstore) == 96, "BSPSTORE offset changed");
static_assert(offsetof(Context, rnat) == 104, "RNAT offset changed");
static_assert(offsetof(Context, caller_unat) == 112, "caller UNAT offset changed");
static_assert(offsetof(Context, spill_unat) == 120, "spill UNAT offset changed");
static_assert(offsetof(Context, lc) == 128, "LC offset changed");
static_assert(offsetof(Context, pr) == 152, "PR offset changed");
static_assert(offsetof(Context, br1_to_br5) == 160, "BR offset changed");
static_assert(offsetof(Context, br1_to_br5) + 1 * sizeof(uint64_t) == 168,
              "BR2 offset changed");
static_assert(offsetof(Context, br1_to_br5) + 2 * sizeof(uint64_t) == 176,
              "BR3 offset changed");
static_assert(offsetof(Context, br1_to_br5) + 3 * sizeof(uint64_t) == 184,
              "BR4 offset changed");
static_assert(offsetof(Context, br1_to_br5) + 4 * sizeof(uint64_t) == 192,
              "BR5 offset changed");
static_assert(offsetof(Context, fpsr) == 200, "FPSR offset changed");
static_assert(offsetof(Context, fp2_to_fp5_and_fp16_to_fp31) == 208,
              "preserved FP image offset changed");
static_assert(offsetof(Context, stack_base) == 528, "stack map offset changed");
static_assert(offsetof(Context, first_entry) == 560, "first-entry offset changed");
static_assert(offsetof(Context, rnat_valid_mask) == 640, "RNAT mask offset changed");
static_assert(offsetof(Context, code_base) == 688, "code-bound offset changed");
static_assert(offsetof(Context, code_limit) == 696, "code-bound limit offset changed");
static_assert(sizeof(Context) == 1024, "context geometry changed");
static_assert(alignof(Context) == 512, "context must align to an RNAT collection");
static_assert(offsetof(ProbeRecord, events) == 8, "event array offset changed");
static_assert(offsetof(ProbeRecord, errors) == 200, "error counter offset changed");
static_assert(offsetof(ProbeRecord, nat_a_before) == 208, "NaT record offset changed");
static_assert(offsetof(ProbeRecord, psr_a_after_resume) == 240, "PSR record offset changed");
static_assert(offsetof(ProbeRecord, fp_a_low) == 256, "FP record offset changed");
static_assert(offsetof(ProbeRecord, pfs_a_first_resume) == 288,
              "first A PFS resume offset changed");
static_assert(offsetof(ProbeRecord, fp_b_first_low) == 320, "B first FP offset changed");
static_assert(offsetof(ProbeRecord, a_final_count) == 336, "final count offset changed");
static_assert(offsetof(ProbeRecord, fp_a_first_resume) == 352,
              "A first-resume FP offset changed");
static_assert(offsetof(ProbeRecord, fp_a_final_resume) == 672,
              "A final-resume FP offset changed");
static_assert(offsetof(ProbeRecord, fp_b_first_entry) == 992,
              "B first-entry FP offset changed");
static_assert(offsetof(ProbeRecord, fp_b_resume) == 1312,
              "B resume FP offset changed");
static_assert(offsetof(ProbeRecord, ec_b_bootstrap) == 1632,
              "bootstrap EC evidence offset changed");
static_assert(offsetof(ProbeRecord, pfs_b_bootstrap) == 1640,
              "bootstrap PFS evidence offset changed");
static_assert(offsetof(ProbeRecord, ec_a_initial) == 1648,
              "initial A EC evidence offset changed");
static_assert(offsetof(ProbeRecord, ec_a_first_resume) == 1656,
              "first A EC resume evidence offset changed");
static_assert(offsetof(ProbeRecord, ec_a_final_resume) == 1664,
              "final A EC resume evidence offset changed");
static_assert(offsetof(ProbeRecord, ec_b_first_entry) == 1672,
              "first B EC evidence offset changed");
static_assert(offsetof(ProbeRecord, ec_b_resume) == 1680,
              "resumed B EC evidence offset changed");
static_assert(offsetof(ProbeRecord, failure_seen) == 1688,
              "failure evidence offset changed");
static_assert(offsetof(ProbeRecord, failure_gp) == 1760,
              "failure GP evidence offset changed");
static_assert(sizeof(ProbeRecord) == 1776, "probe record size changed");
static_assert(sizeof(Event) == 24, "event record geometry changed");

extern "C" Context ia64_coop_context_a;
extern "C" Context ia64_coop_context_b;
extern "C" ProbeRecord ia64_coop_probe_record;

} // namespace ia64_coop_probe
