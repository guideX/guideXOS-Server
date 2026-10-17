do index = 1 to 3
  if length(substr("abc",index,1)) = 1 then say word("one two three",index)
end
say compare(translate("abc"),"ABC")
exit 0