if index('abcdef','cd') <> 3 then exit 1
if index('abcdef','xd') <> 0 then exit 2
if index('abcdef','bc',3) <> 0 then exit 3
if index('abcabc','bc',3) <> 5 then exit 4
if index('abcabc','bc',6) <> 0 then exit 5
if index('ababa','aba',2) <> 3 then exit 6
if index('abc','') <> 0 then exit 7
if index('','a') <> 0 then exit 8
if index('00FF80'X,'FF'X) <> 2 then exit 9
if index('41420041'X,'0041'X) <> 3 then exit 10
if pos('FF'X,'00FF80'X) <> 2 then exit 11
if index('0080FF00'X,'80FF'X) <> 2 then exit 12
say 'RX26_INDEX_BINARY_POS_DONE'
