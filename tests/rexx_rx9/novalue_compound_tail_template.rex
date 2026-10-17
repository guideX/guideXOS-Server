signal on novalue
separator. = '|'
source = 'left|right'
parse var source first (separator.tail) second
say first
say second
exit 0
novalue: say condition('D'); exit 1
