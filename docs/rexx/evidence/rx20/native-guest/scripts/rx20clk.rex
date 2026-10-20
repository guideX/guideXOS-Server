signal on syntax name no_current_date
say date('S')
exit 41

no_current_date:
signal off syntax
say 'RX20_DATE_CURRENT_UNAVAILABLE'
say date('B', '1 Jan 0001', 'N')
say date('B', '29 Feb 2000', 'N')
say date('D', '20240229', 'S')
say date('E', '20240229', 'S')
say date('S', '730178', 'B')

signal on syntax name no_current_time
say time('N')
exit 42

no_current_time:
signal off syntax
say 'RX20_TIME_CURRENT_UNAVAILABLE'

elapsed_before = time('E')
do spin = 1 to 10000
end
elapsed_after = time('E')
if elapsed_after <= elapsed_before then exit 43
say 'RX20_MONOTONIC_E' elapsed_before elapsed_after

reset_before = time('R')
do spin = 1 to 10000
end
reset_after = time('R')
if reset_after <= reset_before then exit 44
say 'RX20_MONOTONIC_R' reset_before reset_after
say 'RX20_NATIVE_CLOCK_CAPABILITY_DONE'
