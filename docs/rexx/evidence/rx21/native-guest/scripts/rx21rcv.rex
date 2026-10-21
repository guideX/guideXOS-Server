signal on syntax name no_current_date
say date('S')
exit 51

no_current_date:
signal off syntax
say 'RX21_NATIVE_DATE_UNAVAILABLE_CAUGHT'
say date('S', '20240229', 'S')
say 'RX21_NATIVE_GUEST_RESPONSIVE'