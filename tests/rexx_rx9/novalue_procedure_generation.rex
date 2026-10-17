signal on novalue
call outer
say item
exit 0
outer: procedure expose item
call inner
say item
return
inner: procedure expose item
value = item
return
novalue:
say condition('D')
say sigl
item = 'linked'
return
