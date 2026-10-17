say descend(3,"x",)
exit 0
descend: procedure
  if arg(1)=0 then return arg(2)||":"||arg(3,"O")||":"||arg()
  return descend(arg(1)-1,arg(2),)