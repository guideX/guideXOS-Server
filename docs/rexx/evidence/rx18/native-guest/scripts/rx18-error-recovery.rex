signal on syntax name invalid_length
value = center('x',-1)
signal off syntax
say 'RX18_NATIVE_ERROR_NOT_CAUGHT'
exit 1

invalid_length:
signal off syntax
say 'RX18_NATIVE_INVALID_ARGUMENT_CAUGHT'
say 'RX18_NATIVE_POST_ERROR_RESPONSIVE:' c2x(centre('abc',7))
exit 0
