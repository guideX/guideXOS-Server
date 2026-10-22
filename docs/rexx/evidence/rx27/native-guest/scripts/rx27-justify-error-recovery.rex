signal on syntax name invalid_arguments
ignored = justify('a b',5,'xx')
say 'RX27_INVALID_JUSTIFY_ARGUMENT_MISSED'
exit 81
invalid_arguments:
signal off syntax
say 'RX27_INVALID_JUSTIFY_ARGUMENT_CAUGHT'
say 'RX27_POST_ERROR_RESPONSIVE:' justify('a b',5)
