if reverse('ABC') <> 'CBA' then exit 61
if c2x(reverse('00FF80'x)) <> '80FF00' then exit 62
if c2x(reverse('414200'x)) <> '004241' then exit 63
if errortext(16) <> 'Label not found' then exit 64
if errortext(60) <> '' then exit 65
line_count = sourceline()
if line_count < 10 then exit 66
if sourceline(1) <> "if reverse('ABC') <> 'CBA' then exit 61" then exit 67
if sourceline_from_routine() <> sourceline(1) then exit 68
say 'RX22_NATIVE_REVERSE: CBA'
say 'RX22_NATIVE_BINARY: 80FF00 004241'
say 'RX22_NATIVE_DIAGNOSTICS: Label not found []'
say 'RX22_NATIVE_SOURCELINE:' line_count sourceline(1)
say 'RX22_NATIVE_CLASSIC_DIAGNOSTIC_DONE'
exit

sourceline_from_routine:
return sourceline(1)
