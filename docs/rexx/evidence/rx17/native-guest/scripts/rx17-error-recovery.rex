signal on syntax name invalid_pad
value = bitxor('13'x,'','')
signal off syntax
say 'RX17_NATIVE_ERROR_NOT_CAUGHT'
exit 1

invalid_pad:
signal off syntax
say 'RX17_NATIVE_ERROR_HANDLED'
say 'RX17_NATIVE_POST_ERROR_RESPONSIVE:' c2x(bitxor('00ff80'x,'0080ff'x))
exit 0
