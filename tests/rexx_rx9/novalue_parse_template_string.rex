signal on novalue
left = 'saved-left'
right = 'saved-right'
parse value 'first|second' with left '|' (missingPattern) right
say 'unreachable'
exit 1
novalue:
say condition('D')
say sigl
say left
say right
exit 0
