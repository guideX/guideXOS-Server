say 'RX13_NATIVE_WORDS_START'
say 'DELWORD:' '['||delword('one  two   three',2,1)||']'
say 'SUBWORD:' '['||subword('one two  three',2)||']'
say 'WORDINDEX:' wordindex('one   two three',3)
say 'WORDLENGTH:' wordlength('one   two three',2)
say 'LASTPOS:' lastpos('ab','abxxab')
interpret 'say "INTERPRET:" left("RX13",6,".")'
say 'RX13_NATIVE_WORDS_DONE'
exit 0
