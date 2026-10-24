signal on syntax name invalid_start
say 'RX33_CHARIN_ARGUMENT_VALIDATION_BEGIN'
ignored = charin('sample',0,1)
say 'RX33_CHARIN_ARGUMENT_UNEXPECTED'
exit 1
invalid_start:
  signal off syntax
  say 'RX33_CHARIN_ARGUMENT_REJECTED'
  exit 0
