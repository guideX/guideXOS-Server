signal on syntax name no_local_clock
say date('S')
exit 1
no_local_clock:
signal off syntax
say 'RX19_NATIVE_CLOCK_UNAVAILABLE_CAUGHT'
say date('S', '20240229', 'S')
say time('E')
signal on syntax name invalid_date
say date('S', '19000229', 'S')
exit 1
invalid_date:
signal off syntax
say 'RX19_NATIVE_INVALID_DATE_CAUGHT'
say date('S', '20240229', 'S')
say 'RX19_NATIVE_GUEST_RESPONSIVE'
