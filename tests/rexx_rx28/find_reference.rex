say FIND('now is the time','is the time')
say FIND('now is  the time','is    the')
say FIND('now is the time','is time')
say FIND('now is the time','the')

say FIND('a b c','a')
say FIND('a b c','b')
say FIND('a b c','c')
say FIND('a b c','d')
say FIND('a b a b','a b')
say FIND('a b a b','b a')

say FIND('ABC DEF','abc')
say FIND('ABC DEF','ABC')
say FIND('abc def','bc')
say FIND('','')
say FIND('abc','')
say FIND('','abc')
say FIND('   ',' ')

say C2X('80 00FF 41'X)
/* Hex-literal grouping blanks are discarded, so this is one binary word. */
say FIND('80 00FF 41'X,'00FF'X)
/* An actual REXX blank separates the NUL/FF word from the 80 byte. */
say FIND('80'X || ' ' || '00FF'X,'00FF'X)
/* Neither NUL nor a high-bit byte terminates a word. */
say FIND('80'X || '00FF'X,'FF'X)
say FIND('41 80'X || ' ' || 'FF42'X,'80'X)
say FIND('41 80'X || ' ' || 'FF42'X,'FF42'X)
