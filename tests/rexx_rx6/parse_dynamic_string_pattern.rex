date = '11/15/90'
parse var date month 3 delimiter +1 day +2 (delimiter) year
say '['||month||']['||delimiter||']['||day||']['||year||']'
