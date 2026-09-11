# Phase 12 Developer Studio IDE proof

This project is opened, edited, built, packaged, and run by the in-OS
guideXOS Developer Studio. The resident compiler emits both ARM64 and AMD64
NativeElf entries into the canonical package; the proof run selects ARM64.

The target is deliberately small so the full workflow can execute from the
bounded freestanding Developer Studio profile during a fresh AARCH64 boot.
