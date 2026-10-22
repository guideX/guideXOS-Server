say 'RX24_ADDRESS_DEFAULT:' address()
address kernel
say 'RX24_ADDRESS_SELECTED:' address()
previous = trace('E')
say 'RX24_TRACE_PREVIOUS:' previous
address kernel rx24_guest_command
say 'RX24_ADDRESS_AFTER_TEMPORARY:' address()
say 'RX24_TRACE_CURRENT:' trace()
say 'RX24_TEMPORARY_RC:' rc
previous = trace('O')
say 'RX24_TRACE_OFF_PREVIOUS:' previous
say 'RX24_TRACE_OFF_CURRENT:' trace()
say 'RX24_ADDRESS_TRACE_DONE'
exit
