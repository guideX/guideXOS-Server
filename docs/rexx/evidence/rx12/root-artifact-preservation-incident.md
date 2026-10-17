# RX12 root artifact preservation incident

The RX12 preflight identified 28 untracked `.obj`/`.pdb` files in the repository root and required their exact bytes to remain unchanged.

The first invocations of `tests/run_rexx_sanitizers.ps1` used MSVC `cl.exe` without `/Fo` or `/Fd`. MSVC therefore emitted object and PDB outputs in the repository root, overwriting several pre-existing artifacts. The root files were not deleted or moved. The original byte contents were not recoverable from another workspace copy or an available shadow copy.

The harness now supplies `/Fo` and `/Fd` paths under each isolated sanitizer run directory on C:. A 32-entry hash manifest captured immediately before the final isolated rerun matched all 32 paths after that rerun. This verifies that the corrected run did not make further root-artifact changes; the manifest was captured after the initial overwrite and does not verify the required original bytes.

The RX12-created `rexx_rx12_builtins_test.obj` was removed after confirming it was not one of the 28 pre-existing paths. The 28 pre-existing artifact paths remain in the root and remain untracked. No automated cleanup, move, restore, or replacement was applied to those paths. RX12 is recorded as Outcome B because the exact-preservation requirement was violated and could not be repaired.
