sample = charin('rx34sample',1,1)
say 'RX34_ERROR_FIXTURE_BEGIN'
signal on syntax name provider_error
ignored = charin('fault')
say 'RX34_PROVIDER_ERROR_UNEXPECTED_RETURN'
exit 1

provider_error:
  signal off syntax
  say 'RX34_PROVIDER_ERROR=' || stream('fault','D')
  say 'RX34_UNKNOWN=' || stream('missing')
  signal on syntax name missing_error
  ignored = charin('missing')
  say 'RX34_MISSING_UNEXPECTED_RETURN'
  exit 1

missing_error:
  signal off syntax
  say 'RX34_MISSING=' || stream('missing')
  signal on syntax name noseek_error
  ignored = charin('noseek',1,1)
  say 'RX34_NOSEEK_UNEXPECTED_RETURN'
  exit 1

noseek_error:
  signal off syntax
  say 'RX34_NOSEEK=' || stream('noseek','D')
  signal on syntax name invalid_position
  ignored = charin('rx34sample',99,1)
  say 'RX34_INVALID_POSITION_UNEXPECTED_RETURN'
  exit 1

invalid_position:
  signal off syntax
  say 'RX34_INVALID_POSITION_PRIOR=' || stream('rx34sample','D')
  signal on syntax name invalid_argument
  ignored = charin('rx34sample',0,1)
  say 'RX34_INVALID_ARGUMENT_UNEXPECTED_RETURN'
  exit 1

invalid_argument:
  signal off syntax
  say 'RX34_INVALID_ARGUMENT_PRIOR=' || stream('rx34sample','D')
  say 'RX34_OVERSIZED_REQUEST_BEGIN'
  ignored = charin('rx34sample',1,16777217)
  say 'RX34_OVERSIZED_REQUEST_UNEXPECTED_RETURN'
  exit 1
