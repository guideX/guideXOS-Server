signal on syntax name invalid_trace
old = trace('Z')
say 'RX24_INVALID_TRACE_MISSED'
exit 81
invalid_trace:
signal off syntax
say 'RX24_INVALID_TRACE_RECOVERED:' trace()
say 'RX24_GUEST_RESPONSIVE'