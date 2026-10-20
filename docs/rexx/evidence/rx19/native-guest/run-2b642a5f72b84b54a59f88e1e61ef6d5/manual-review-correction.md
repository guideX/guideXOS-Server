# RX19 QEMU run correction

This first isolated run is preserved as evidence, but its automated `passed` flag was incorrect because the harness checks that screenshots were captured without checking their visible contents. Manual screenshot review found that `rx19-date-conversions.rex` requested `DATE('W', input_date, 'S')`, which is not valid under the selected z/OS profile because Weekday is a current-date-only output. The guest reported an invalid built-in argument at instruction 28. The guest remained responsive and QMP shutdown was clean.

The result JSON now retains `automated_checks_passed: true` to record the harness result, sets the phase result to failed, and links the corrected run. RX19 acceptance uses the visually reviewed rerun at `run-758845fd006940f2a2263c08f42be306`.