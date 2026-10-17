say 'RX12_NATIVE_STRINGS_START'
say 'Length:' length('hello')
say 'Substr:' substr('abcdef',2,3)
say 'POS:' pos('REXX','guideXOS REXX scripting')
say 'WORDS:' words('one   two three')
say 'WORD:' word('one two three',2)
say 'WORDPOS:' wordpos('is the','now is the time')
say 'STRIP:' '['||strip('  abc  ')||']'
say 'SPACE:' '['||space('one   two',0)||']'
say 'COPIES:' copies('ab',3)
say 'TRANSLATE:' translate('abbc','&','b')
say 'COMPARE:' compare('AB','AB ')
say 'VERIFY:' verify('AB3','AB')
interpret 'say "INTERPRET:" length(strip("  code  ")) + wordpos("is the", "now is the")'
say 'RX12_NATIVE_STRINGS_DONE'
exit 0