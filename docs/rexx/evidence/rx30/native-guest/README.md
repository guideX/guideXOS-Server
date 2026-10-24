# RX30 Native Guest Evidence

Three complete QAC acceptance attempts passed and remain separately preserved. The first used a longer demo whose opening output scrolled above the Console viewport. The second repeated the suite with compact output. The third reran the compact suite against the final freestanding kernel after the parser AST cleanup correction.

The final acceptance run is `run-ab53cec6398c44d7bc90cfdbad1ec60c/`. Its screenshots visibly show QUEUE/PUSH order and counts, uppercase PULL and case-preserving PARSE PULL, empty-string handling, placeholder templates, routine/INTERPRET sharing, deterministic empty-input failure, post-error responsiveness, DATE/TIME fail-closed behavior, and RX20 monotonic clock/PIT progression. The final kernel SHA-256 is `f3647c4b3efa9c9a01d96ab4e440bf37aaf15a0044181315292833584ad5fac9`; guest smoke evidence matched the staged kernel.

For the final run, QAC parent PID 18744 exited 0, owned QEMU child PID 19160 exited 0, and the runner exited 0. QMP acknowledged shutdown; reservation `ee04c6649ec343f4bbec21ec8a4316dc` was released; QAC reported zero managed sessions and the final QEMU census was zero. The suite's PIT heartbeat count advanced from 2 to 116. QAC used networking disabled, RX30-specific disposable media, isolated QMP resources, and verified ownership before controlling the child. No foreign QEMU process was controlled.

Readiness images and visual review records show the Console prompt open, focused, and echoing `RX30_CONSOLE_READY` before scripts were sent. Guest scripts reside in `scripts/`; `hosted-script-validation.log` records host-side exit/output checks. Each attempt independently verified all 31 protected root artifacts.
