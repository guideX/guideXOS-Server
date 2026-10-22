signal on syntax name invalid_start
ignored = index('abc','a',0)
say 'RX26_INVALID_START_MISSED'
exit 81
invalid_start:
signal off syntax
say 'RX26_INVALID_START_CAUGHT'
say 'RX26_INDEX_POST_ERROR_RESPONSIVE:' index('abcabc','bc',3)
