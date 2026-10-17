delimiter = ''
parse value 'alpha beta' with before (delimiter) after
say '['||before||']['||after||']'
