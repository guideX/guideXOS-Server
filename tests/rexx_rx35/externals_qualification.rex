say 'RX35_EXTERNALS_BEGIN'
say 'RX35_EXTERNALS_NORMAL=' || externals()
say 'RX35_EXTERNALS_EMPTY_BUFFER=' || externals()
numeric digits 1
numeric fuzz 0
numeric form engineering
say 'RX35_EXTERNALS_NUMERIC=' || externals()
say 'RX35_EXTERNALS_REPEAT=' || externals() || '/' || externals()
queue 'RX35_STACK_ONLY'
say 'RX35_EXTERNALS_STACK_BOUNDARY=' || externals() || '/' || queued()
pull stack_only
call rx35_probe
interpret "RX35_DYNAMIC = EXTERNALS()"
say 'RX35_EXTERNALS_INTERPRET=' || RX35_DYNAMIC
signal on syntax name invalid_call
ignored = externals('')
say 'RX35_EXTERNALS_INVALID_UNCAUGHT'
exit 9

rx35_probe: procedure
call rx35_leaf
say 'RX35_EXTERNALS_ROUTINE=' || externals()
return

rx35_leaf: procedure
say 'RX35_EXTERNALS_NESTED=' || externals()
return

invalid_call:
signal off syntax
say 'RX35_EXTERNALS_INVALID_CAUGHT'
say 'RX35_EXTERNALS_RECOVERY=' || externals()
exit
