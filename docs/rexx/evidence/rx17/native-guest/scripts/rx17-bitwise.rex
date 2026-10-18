if c2x(bitand('73'x,'27'x)) <> '23' then exit 1
if c2x(bitand('13'x,'5555'x)) <> '1155' then exit 2
if c2x(bitand('13'x,'5555'x,'74'x)) <> '1154' then exit 3
if c2x(bitor('15'x,'2456'x,'f0'x)) <> '35F6' then exit 4
if c2x(bitxor('1111'x,'444444'x,'40'x)) <> '555504' then exit 5
if c2x(bitand('13'x,,'00'x)) <> '00' then exit 6
if c2x(bitor('13'x,,'80'x)) <> '93' then exit 7
if c2x(bitxor('13'x,,'ff'x)) <> 'EC' then exit 8
if c2x(bitxor('00ff80'x,'0080ff'x)) <> '007F7F' then exit 9
say 'RX17_NATIVE_BITWISE_DONE'
