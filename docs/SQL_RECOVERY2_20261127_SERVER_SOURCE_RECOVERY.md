# SQL_RECOVERY2_20261127 — Server R3BM Source Recovery

**Status: partial reconstruction.** This branch preserves recovered Server-side Native SQL integration material for review and future continuation. Its commits are new recovery commits and do not recreate or claim the missing original Git objects or ancestry. It is not a full Server integration qualification.

## Repository and provenance

- Repository: `guideXOSServer`, remote `git@github.com:guideX/guideXOS-Server.git`.
- Recovery branch base: `f29850c93bbac0a0763a31e806d2816310156575`, the verified historical `origin/NATIVE_DB_STORAGE` tip. This is a candidate preservation base only; ancestry from the recorded original R3BM parent is unverified.
- Recovery1 reports the original Server R3BM commit as `ee58da1979084d10703161b00f6e0ae4a13aa452`, parent `e9f45aecc7c11f92d666c221f1cd7c3ab24e6556`. The parent/commit objects were unavailable in the checked Git object databases and GitHub. Do not infer a complete diff or original ancestry from this branch.
- The source set draws from the 26-file verified Recovery1 Server snapshot, eight additional files present in the named C: source worktree but omitted from that focused manifest, and one further dependency discovered during fresh linking (`kernel/core/native_database_file.cpp`). The three copies of that dependency (original source, Recovery2 context snapshot, and this worktree) have identical SHA-256 `7e2da8848ca0ddc40ce3b901883ae82d1c8141ed8d66fc36e362289c34100fd3`; comparison with the selected base showed the narrow read-only helper addition required by the VFS harness.
- The 35 reviewed source paths include `kernel/core/include/kernel/vfs.h`, which is byte-identical to the selected base and therefore produces no change. The other 34 source files are changed in the implementation commit. The full source file list and SHA-256 values are recorded below. Per-file copy verification is also retained in `E:\CodexArtifacts\SQL_RECOVERY2_20261127\server-reconstructed-source-manifest.csv` and `server-extra-dependency-manifest.csv`.

## Recovered material

The recovered files cover SQL host callbacks and catalog projection, read-only service dispatch and diagnostics, FAT/VFS integration, Native ELF SDK declarations and sample, ABI checks, hosted tests, and focused runner scripts. This is a selected source recovery, not proof that all build dependencies or historical surrounding changes are present. In particular, earlier R3AH SQL host/service integration history is not established by the selected Server base.

| Repository path | Bytes | SHA-256 |
|---|---:|---:|---|
| `kernel/core/native_sql_application_service.cpp` | 42395 | `a8ef18810c415d4327b417e4428e3fc6a4035a397555613a6fbef903ed99a0c2` |
| `kernel/core/native_sql_read_diagnostic.cpp` | 18731 | `864a20b7df8dc1ac037233420ccb82517ca6307abbbbb62828a84b62aba8fbaa` |
| `kernel/core/include/kernel/native_sql_application_service.h` | 1323 | `858051d0b56b2c60263660ae803ff4fd651807b1ddb7ddeb2dc17db13c1e1382` |
| `kernel/core/include/kernel/native_sql_qualification_gate.h` | 564 | `bbe0ece8284a5b0096d5767e1bef7d9383cacb78123d7a58a3c346d4c964a1e4` |
| `kernel/core/include/kernel/native_sql_read_diagnostic.h` | 1959 | `2d880ee118a58ed9abb229eec0b30042c252401ce9ef2c57b4354039c9da3d41` |
| `kernel/core/fs_fat.cpp` | 149416 | `2d6b2278e1ef262b8d8646adad7206590c6129189e434a88210483db00347302` |
| `kernel/core/include/kernel/fs_fat.h` | 18599 | `9b07908eb9e8a54320eb88df235b77fb24aeb73a7bc75cf8a261dfc2fd80f797` |
| `kernel/core/vfs.cpp` | 65937 | `259e71de3e2b639c6cf61d4512b578972d78ddfeee71becc4aa8968f388f6a60` |
| `kernel/core/include/kernel/vfs.h` | 12307 | `118f26b6bfea5ab10f2a0e6f63545db1c84cbbfd602ee7d1f5bf7672d7610ce6` |
| `tests/native_sql_r3ai_vfs_diagnostic_test.cpp` | 7313 | `e0eb62440d956f4a51f7b22784871cafa9a6ba54159c317b0a498e65e25dfe1a` |
| `tests/native_sql_r3ak_mount_regression_test.cpp` | 20329 | `6d067fea6f11b605f718474533f73a98e58180ede6cfa93991b273a7f71762da` |
| `tests/native_sql_r3am_ramdisk_provenance_test.cpp` | 15303 | `ce57d2d16a55896ac068616dd3c054b34a125d45d7af156f336ab923caa0977a` |
| `tests/native_sql_r3aq_query_service_test.cpp` | 61403 | `b6a95640a55e26920f3da8d0419810fdb686110b2d70d003684cefe735fd48a7` |
| `tests/native_sql_r3ar_fat32_vfs_test.cpp` | 8262 | `e080fd642ee037bd51415c47bbf4818f177f6a07753c2baa8ac83a969ab6fa12` |
| `tests/native_sql_r3aw_manifest_read_test.cpp` | 17392 | `8db3d4f6f7ae31a9e4e29a966da59549c35b4b100837466c95d050a2a965378b` |
| `tests/native_sql_r3bc_application_api_test.cpp` | 57011 | `7fc97cf34c878dad9c79e7a36acf4c4bca92b610efd255a56a998e3087ca2b09` |
| `tests/native_abi_layout_test.cpp` | 13921 | `4c099e51416f56a5080bfec3c55e8734953202e13d85239c3ad3156874d691b0` |
| `sdk/include/guidexos/sql.h` | 4122 | `c56c14910266852c146d80a906b4c241bf77e7c4edde373ab564076bd06f165e` |
| `sdk/include/guidexos/abi.h` | 9032 | `50fa9e361cd6465c10effc06c2b1ffc59b48abe41b0debdc1a1a698956b58eb1` |
| `sdk/samples/sqlread/main.cpp` | 53060 | `05e731e530dabaf301d15bd573cdbde600fe2078e383b1eb550b7ed4dc95b76a` |
| `sdk/samples/sqlread/edge32_oracle.h` | 2885 | `23df0bd6d66eaf057eaa283a5f6d7207440a055c40b35c3923a6efcef78ab680` |
| `sdk/samples/sqlread/edge64_oracle.h` | 3129 | `34026884bea9eaee6a0ea58303be9e8a53614db9e110e87c05dd86699de41006` |
| `sdk/samples/sqlread/app.json` | 719 | `b83037c8eefcf3f532115acb1c13f367ad027b54a3e91b02694a7bfce8ec3856` |
| `sdk/samples/sqlread/README.md` | 2404 | `bf834922d100fc8992d444e47c522afac5897282043f93211087736ee6aeb64b` |
| `sdk/README.md` | 4885 | `2c7dce3f62477a73ffd8f4201dd41933747610b636dce9490f1318a0947de960` |
| `sdk/build-samples.ps1` | 10038 | `df2241d1a96c61c9c69e4d30295d9c960337b43aafd5eab0f60c20d4068b29cb` |
| `kernel/Makefile` | 14346 | `f047fd89b1d9bce45356c874447795f443a264566f9aa39968b529f47e880828` |
| `kernel/core/native_elf_baremetal.cpp` | 94199 | `9df9a6a37c25068d97f3ea46ca18b8629ed38ab348515e50b6ebeb5f17af2894` |
| `kernel/core/include/kernel/native_elf_baremetal.h` | 3118 | `c34166649b860da400cd348bc609fe22bfe5026cfdab53481bb01fe43d742cc3` |
| `sdk/include/guidexos/app.h` | 1283 | `47c40f47f5d1a98a9d9ddd0d4a2f375733ec698367c2397ec71cb5d053498ee5` |
| `scripts/run-native-sql-r3aq-query-service-test.ps1` | 3741 | `6f76c3cc6dc091e665a4d4e8b18831c5d46cfe94bf20bd20bece8129d85f2497` |
| `scripts/run-native-sql-r3ar-fat32-vfs-test.ps1` | 3815 | `1586ed77a17be192e696ffd257d1ca11cb14ef7b57866257de82b1d74c6b8e35` |
| `scripts/run-native-sql-r3bc-application-api-test.ps1` | 3859 | `509fb98fe2c5f93ea71bb5cec4886bd8066e214ff9b63e01c4c960b5bc91c379` |
| `scripts/run-native-sql-r3be-application-lifecycle-test.ps1` | 3937 | `1444309b2934395fa9616bd8118e91d7ef24d1aa99a82143fd037241635a366a` |
| `kernel/core/native_database_file.cpp` | 19905 | `7e2da8848ca0ddc40ce3b901883ae82d1c8141ed8d66fc36e362289c34100fd3` |

## Fresh checks

- Native ABI layout test: PASS. Public callback offsets are 256, 264, 272, 280 and 288; legacy prefix 288 bytes; current host table 296 bytes; `gx_sql_key` 16 bytes; `gx_sql_range` 48 bytes.
- Native ELF `SQLReadDemo` SDK build: PASS; produced an AMD64 ELF with no global undefined symbols.
- R3AR production FAT32/VFS hosted check: PASS, 17/17. It used the preserved R3AR RAM disk, the unchanged 250,880-byte SQL fixture, and the canonical LF manifest exported from its committed Git blob. The Windows worktree manifest has CRLF conversion; that working copy was not used as the trusted input and no fixture bytes were changed.
- The initial R3AR link attempt exposed that Recovery1's focused Server manifest omitted `gx_db_file_is_read_only`; after restoring the exact source file described above, the hosted check passed. Warnings were an unused FAT helper and an existing ext4 name-length comparison.

## Qualification gaps

- Six-index SQL checks pass in the paired SQL recovery worktree, but this Server branch has not completed the full integrated acceptance contract.
- R3AQ, R3BC, and R3BE focused Server tests were not freshly run because the exact controlled missing-fixture RAM disk inputs are absent from the preserved R3BM inventory. No substitute oracle or image was created.
- App Model guest acceptance, freestanding qualification link/build, and ordinary/qualification AMD64 builds remain unverified in this recovery. Historical logs are not fresh qualification.
- R3BH guest acceptance remains pending; R3BB Gate 9 remains `UNVERIFIED`; the frozen R3BE ISO was hash-checked and left unchanged. No QAC admission or QEMU launch was performed.

## Handling

Keep this branch local until its parent and complete Server source provenance are resolved. Do not merge it into Server main or treat it as an ancestry-verified Server release. Recommended next phase: recover the missing R3AH/R3BM Server history or establish a complete source baseline, restore the controlled missing-fixture test inputs, then run App Model and freestanding checks before considering broader hosted qualification.
