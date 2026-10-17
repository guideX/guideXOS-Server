call on error name handleError
address KERNEL "positive"
say length(condition("C"))
exit 0

handleError:
call inspectInfo
say condition("C")
say condition("D")
return

inspectInfo:
say condition("C")
say condition("D")
return
