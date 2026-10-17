call inspect "alpha",,""
exit 0
inspect: procedure
  code = 'say arg(2,"O")||":"||wordpos(substr(arg(1),1,5),"alpha beta")'
  interpret code
  return