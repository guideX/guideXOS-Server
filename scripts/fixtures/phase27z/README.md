# Phase 27Z bare-metal entry-debugger fixture

This compiler-built NativeElf is launched through the same Developer Studio
Run service as Phase 27Y. Its first user-visible operation is a single host
log call, followed by the normal compositor-backed GUI lifecycle. The focused
smoke starts it with the one `gx_main` entry breakpoint, verifies a real
paused snapshot before the log or GUI exists, resumes it, and then exercises
normal close and paused cancellation.
