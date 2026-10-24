say 'RX34_NO_PROVIDER_BEGIN'
say 'RX34_NO_PROVIDER_STATE=' || stream()
signal on syntax name unavailable_handler
ignored = charin()
say 'RX34_NO_PROVIDER_UNEXPECTED_RETURN'
exit 1

unavailable_handler:
  signal off syntax
  say 'RX34_NO_PROVIDER_FAIL_CLOSED'
  say 'RX34_NO_PROVIDER_STATE_AFTER=' || stream()
  exit 0
