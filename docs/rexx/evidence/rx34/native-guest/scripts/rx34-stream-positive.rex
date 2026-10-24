signal on syntax name eof_handler
say 'RX34_STREAM_FIXTURE_BEGIN'
say 'RX34_DEFAULT=' || c2x(charin())
say 'RX34_FIRST=' || c2x(charin('rx34sample',1,3))
say 'RX34_SECOND=' || c2x(charin('rx34sample',,2))
ignored = charin('rx34sample',1,0)
say 'RX34_ZERO_REPOSITION=' || c2x(charin('rx34sample',,2))
position_five = charin('rx34sample',5)
say 'RX34_POSITION_FIVE=' || c2x(position_five)
say 'RX34_POSITION_FIVE_STATE=' || stream('rx34sample','D')
position_continue = charin('rx34sample')
say 'RX34_POSITION_CONTINUE=' || c2x(position_continue)
say 'RX34_EXACT_END_STATE=' || stream('rx34sample','D')
ignored = charin('rx34sample',3,0)
binary_read = charin('rx34binary',,5)
say 'RX34_BINARY_LENGTH=' || length(binary_read)
say 'RX34_BINARY_HEX=' || c2x(binary_read)
short_read = charin('rx34sample',,8)
say 'RX34_SHORT_LENGTH=' || length(short_read)
say 'RX34_SHORT=' || c2x(short_read)
say 'RX34_SHORT_STATE=' || stream('rx34sample','D')
ignored = charin('rx34sample',,1)
say 'RX34_EOF_UNEXPECTED_RETURN'
exit 1

eof_handler:
  signal off syntax
  say 'RX34_EOF=' || stream('rx34sample','D')
  exit 0
