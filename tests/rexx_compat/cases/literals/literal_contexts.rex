payload = '00FF80'X
if payload = '00FF80'X then say 'MATCH'
if payload \= '00FF00'X then say c2x(payload)
say 'C2X'('A')
say c2x('41'X(2))
code = "say c2x('41'X)"
interpret code
