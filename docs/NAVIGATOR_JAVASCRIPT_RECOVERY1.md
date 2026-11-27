# Navigator JavaScript Recovery1 Ledger

**Recovery checkout:** `E:\dev\guideXOSServer_NAVIGATOR_JAVASCRIPT`

**Target branch:** `NAVIGATOR_JAVASCRIPT_SUPPORT`

**Audit date:** 2026-11-26 (America/Los_Angeles)

## Repository audit

The checkout root is the directory above. `origin` fetches from and pushes to
`git@github.com:guideX/guideXOS-Server.git`. The fresh checkout was on
`NAVIGATOR_JAVASCRIPT_SUPPORT` at JS128
`86ee961d3d14ca27bec8cbae25a00d237c64fd92`, with a clean worktree. GitHub's
target branch checkpoint was independently confirmed at that same JS128
commit.

The clone is not shallow. Its initial connectivity check passed, and the
initial full unreachable-object inspection produced no unreachable-object
records. There is no configured Git alternate. The clone's initial reflogs
contained only clone-created entries; no historical JS129 reflog entry was
available. The initial local branch list contained only the target branch;
there were no local-only commits, stashes, or additional worktrees. The
The source recovery commit was one local commit ahead of `origin`, with no
uncommitted source changes. Recovery1 adds a separate documentation commit;
the final branch is two local commits ahead of `origin` and is not pushed.

The Git object database initially contained one pack of 706.68 MiB. The
checkout's recursive file-size sum was approximately 1,254.78 MiB across
4,255 files. E: was NTFS with approximately 5.63 TB free at the audit. These
are file-size sums, not a claim about allocated disk blocks.

The reported JS129 commit could not be read with `git cat-file`, did not occur
in the inspected refs/reflogs/unreachable objects, and did not resolve through
the GitHub commit lookup. Accessible E: sibling guideXOS checkouts and the
JS129 recovery artifacts yielded no original source copy. The historical D:
checkout path was inaccessible on its single check. No repeated access to
that path was attempted. This does not make claims about unavailable drives or
uninspected external backups.

## Phase ledger

JS115–JS128 source, reports, and tracked qualification harnesses are present in
the target branch history, which was on GitHub at JS128 before recovery. The
external artifact roots are separate from Git history; their presence does
not by itself mean source or a passing qualification survived.

| Phase | Last reported outcome | Known commit | Recovery classification |
|---|---|---|---|
| JS115 | Portable frontend partial | `3b5c3e45cd7392e020ef1815835f3e40cea91e21` | Source, report, and tests present in target history and local checkout. |
| JS116 | Portable parser verified | `a0c98d55d44cea3552b1eb186921d373e82ce479` | Source, report, and tests present in target history and local checkout. |
| JS117 | Kernel object format partial | `cb7a5a913055959c2910fcdf0ad82a77632ed8e8` | Source and report present; full kernel-build prerequisite remained open. |
| JS118 | Portable values and environments verified | `60023951028cfef195fd30254d0e45950b444885` | Source, report, and tests present in target history and local checkout. |
| JS119 | Portable execution core verified | `75759117d491c3169caa5d7da75b1fac762f9b62` | Source, report, and tests present in target history and local checkout. |
| JS120 | Portable objects and Arrays partial | `b05a673c72b901d0c5b50f2a87c609c449159856` | Source and report present; JS121 later closed the shared-store integration gap. |
| JS121 | Shared object model unified | `1922828e4d6efc1ba69ce281b90fad490ec6bbb9` | Source, report, and tests present in target history and local checkout. |
| JS122 | Portable JSON partial | `0e5af83abaa77b073923868a7efee9966edf529c` | Source, report, and tests present; numeric conversion was not yet portable. |
| JS123 | Portable JSON numeric serializer partial | `6628332857aa52b2ab8107bb5163973ce6be75e9` | Source, report, and tests present; complete portable Value serialization remained open. |
| JS124 | Portable JSON serializer verified | `2e90975e47c5ff9b0db9d361b64cd6e19b3afacc` | Source, report, and tests present in target history and local checkout. |
| JS125 | Portable callback foundation partial | `a33856ee07fc2b86b5c24e0c61c6dba4babf3f63` | Source, report, and tests present; shared external call entry remained open. |
| JS126 | Callable identity and stack partial | `7ff8e2c031a094241c502b1a311fdc82600314e4` | Source, report, and tests present; general non-tail recursion remained open. |
| JS127 | Non-tail continuations partial | `3c142f24649380dd24ce022cf8d6179f393bb3a3` | Source, report, and tests present; only the documented continuation subset was iterative. |
| JS128 | General resumable execution partial | `86ee961d3d14ca27bec8cbae25a00d237c64fd92` | Verified GitHub checkpoint and complete local source/report/tests. External `invokeCallable()` remained recursive. |
| JS129 | Historically reported unified portable call entry verified | Original reported hash `f9aaba0f7427f5f38600233e1fe70e7539764d85` not recovered; reconstruction `ff727380a13bf4ed367b895330ea87449c35de63` | Historical binaries, objects, scripts, and logs survive under the JS129 artifact root. Original C++ source, focused tests, and report were not found. A new source reconstruction was committed and independently verified; it is not byte-identical to the reported source. |
| JS130 | Development prompt prepared; no confirmed completion commit | None confirmed | Planning artifacts are present. No JS130 implementation or qualification is accepted or started by this recovery. |

## JS129 recovery disposition

The reconstruction is commit
`ff727380a13bf4ed367b895330ea87449c35de63`, directly parented by JS128. It
uses the existing portable execution core and stores, unifies script and
external user-call entry on the resumable machine, and adds bounded iterative
capability preflight. The post-commit focused suite passes 16/16; the
AMD64 freestanding compile, link, symbol, and stack-usage audit passes. The
current stack figures and historical artifact measurements are separated in
the [JS129 recovery qualification report](NAVIGATOR_JAVASCRIPT_JS129_UNIFIED_CALL_ENTRY_PREFLIGHT.md).

The whole guest execution stack is not qualified, the recursive hosted
evaluator remains outside the freestanding claim, and no guest/QEMU execution
was performed. JS130 portable JSON `toJSON`/reviver callbacks remain gated on
review of this recovery.
