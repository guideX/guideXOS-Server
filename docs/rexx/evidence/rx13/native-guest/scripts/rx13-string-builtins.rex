say 'RX13_NATIVE_STRINGS_START'
say 'LEFT:' left('AB',4,'.')
say 'RIGHT:' right('42',5,'0')
say 'INSERT_DEFAULT:' insert('XX','abcd',2)
say 'INSERT_EXPLICIT:' insert('123','abc',5,6,'+')
say 'OVERLAY_REPLACE:' overlay('XX','abcd',2)
say 'OVERLAY_EXTEND:' overlay('123','abc',5,6,'+')
say 'DELSTR:' delstr('abcdef',3,2)
say 'RX13_NATIVE_STRINGS_DONE'
exit 0
