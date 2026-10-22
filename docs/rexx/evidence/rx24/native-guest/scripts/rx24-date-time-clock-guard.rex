signal on syntax name current_date_unavailable
say date('S')
exit 81
current_date_unavailable:
signal off syntax
say 'RX24_DATE_CURRENT_UNAVAILABLE'
signal on syntax name current_time_unavailable
say time('N')
exit 82
current_time_unavailable:
signal off syntax
say 'RX24_TIME_CURRENT_UNAVAILABLE'
elapsed_before = time('E')
do spin = 1 to 20000
end
elapsed_after = time('E')
if elapsed_after <= elapsed_before then exit 83
say 'RX24_DATE_TIME_FAIL_CLOSED_PIT_ALIVE'