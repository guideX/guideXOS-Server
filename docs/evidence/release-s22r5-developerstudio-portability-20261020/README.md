# S22R5 evidence inventory

Date: 2026-10-20

The source checkouts were inspected read-only. No production source, SDK, app manifest, binary, QAC artifact, generated output, or reference checkout was modified.

## Starting states

- v0.3: D:\dev\guideXOSServer, main, HEAD 0965387b970254035ffce81f1e58aa6893c57936; tracked/staged clean; 853 pre-existing untracked paths. Expected S22R4 2fde8142a183ea7a4c1ba39bc764c604c0484fcc is the parent. The one added commit is QAC/documentation/launcher work.
- v0.5: D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO, v0.5_DEVELOPER_STUDIO, HEAD b3753c5db1d9bac9fcb0d6a608c45b00078bf39b; clean.
- Modern DeveloperStudio: D:\dev\guideXOS_Developer_Studio, main, HEAD 9b66725990b604d426ef6d2be02e7097a0f9a46e; clean.

The paired v0.5 rehearsal fb71e36c77866cdabf20873e6ad3de0ddc3bbe02 is an ancestor; current HEAD is 97 commits later. The v0.5 package manifest was last changed at ec7a48a33e3a073a0037bcf146dd61a2415bbd47.

## Active validation observed

QAC CLI PID 20380, coordinator PID 408, and QEMU PID 712 belonged to a separate Server V1.1 QAC run with evidence under E:\guideXOS_C181R1_evidence\guest-runs\focused-r1-reset-repro10-qac-sequence. The workload was left untouched.

## Inspection limits

Read-only source, Git history, tests, package and prior-report inspection. No tests, builds, ISO creation or QEMU run were performed. No evidence here claims production guest acceptance for a v0.3 DeveloperStudio implementation.

See ../RELEASE_S22R5_DEVELOPERSTUDIO_SERVICE_PORTABILITY.md for implementation findings and source line references. source-checksums.sha256 records hashes of inspected source, package and binary. The modern and v0.5 manifests match; that does not prove source-to-ELF provenance.
