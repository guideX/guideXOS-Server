signal on novalue
call recurse 2
say shared
exit 0
recurse: procedure expose shared
parse arg depth
if depth > 0 then do
next = depth - 1
call recurse next
end
value = shared
return
novalue:
say condition('D')
say sigl
shared = 'set'
return
