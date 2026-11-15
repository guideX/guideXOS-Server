# AArch64 Run Qualification History

This index tracks the boundary between hosted implementation evidence and native guest acceptance. Historical reports and frozen candidates remain the source of truth for their own results.

| Phase | Outcome | Evidence |
| --- | --- | --- |
| RUNSAFE1 | Child-isolation foundation | [RUNSAFE1 report](AARCH64_RUNSAFE1_CHILD_ISOLATION_FOUNDATION.md) |
| RUNSAFE2 | EL0 memory and exception controls | [RUNSAFE2 report](AARCH64_RUNSAFE2_EL0_MEMORY_EXCEPTION.md), [negative qualification](AARCH64_RUNSAFE2_EL0_NEGATIVE_QUALIFICATION.md) |
| RUNSAFE3-A | Hosted scheduled EL0 foundation | [RUNSAFE3-A report](AARCH64_RUNSAFE3A_SCHEDULED_EL0_FOUNDATION.md) |
| RUNSAFE3-B | Native scheduled EL0 repeatability verified on the preserved frozen candidate | [RUNSAFE3-B report](AARCH64_RUNSAFE3B_NATIVE_SCHEDULING.md) |
| RUNSAFE4-A | Immutable Build identity and isolated ELF loader foundation; Studio Run remains disabled; native compiled-ELF execution not yet established | [RUNSAFE4-A report](AARCH64_RUNSAFE4A_BUILD_ARTIFACT_EL0_LOADER.md) |

RUNSAFE4-A does not revise or replace the three RUNSAFE3-B native acceptance records or their frozen candidate. The next native acceptance belongs to RUNSAFE4-B and requires a source-matched candidate plus QAC-supervised guest evidence. Hosted parser fixtures, compiler output validation, and scheduler simulation do not count as guest execution.
