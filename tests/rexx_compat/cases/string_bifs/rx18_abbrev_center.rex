if abbrev('Print','Pri') <> 1 then exit 1
if abbrev('PRINT','Pri') <> 0 then exit 2
if abbrev('PRINT','PRI',4) <> 0 then exit 3
if abbrev('PRINT','',1) <> 0 then exit 4
if abbrev('00FF80'X,'00FF'X) <> 1 then exit 5
if abbrev('00FF80'X,'00FF81'X) <> 0 then exit 6
if c2x(center('abc',7)) <> '20206162632020' then exit 7
if c2x(center('abc',6,'00'X)) <> '006162630000' then exit 8
if c2x(centre('abc',8,'00'X)) <> '0000616263000000' then exit 9
if centre('The blue sky',8) <> 'e blue s' then exit 10
if centre('The blue sky',7) <> 'e blue ' then exit 11
if center('abcdef',3) <> 'bcd' then exit 12
if center('',4,'-') <> '----' then exit 13
numeric digits 6
numeric_before = 1/7
abbrev_value = abbrev('Print','Pri')
center_value = centre('abc',7)
numeric_after = 1/7
if numeric_before <> numeric_after then exit 14
say '1'
say '0'
say '0'
say '0'
say '1'
say '0'
say '20206162632020'
say '006162630000'
say '0000616263000000'
say 'e blue s'
say '['||centre('The blue sky',7)||']'
say 'bcd'
say '----'
say 'RX18_PORTABLE_STRING_BUILTINS_DONE'
