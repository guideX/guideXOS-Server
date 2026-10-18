value = 'set'
stem.tail = 'item'
call inspect
exit 0
inspect: procedure expose value stem.
say 'SYMBOL_SCOPE:' symbol('value') symbol('stem.tail') symbol('stem.other') symbol('not_set')
say 'RX14_NATIVE_SYMBOL_DONE'
