if justify('The blue sky',14) <> 'The  blue  sky' then exit 1
if justify('The blue sky',8) <> 'The blue' then exit 2
if justify('The blue sky',9) <> 'The  blue' then exit 3
if justify('The blue sky',9,'+') <> 'The++blue' then exit 4
if justify('a b c',9) <> 'a   b   c' then exit 5
if justify('a b c',8,'-') <> 'a---b--c' then exit 6
if justify('  a   b  ',5,'.') <> 'a...b' then exit 7
if justify('abc def',3) <> 'abc' then exit 8
if justify('a b',3) <> 'a b' then exit 9
if c2x(justify('a b',5,'00'x)) <> '6100000062' then exit 10
if justify('',7) <> '' then exit 11
if justify('solo',8) <> 'solo' then exit 12
if justify('solo',0) <> '' then exit 13
if justify('a b c',4,'.') <> 'a..b' then exit 14
say 'RX27_JUSTIFY_DONE'
