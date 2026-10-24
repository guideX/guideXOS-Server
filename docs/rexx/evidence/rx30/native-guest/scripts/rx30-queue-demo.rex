initialCount = queued()
if initialCount <> 0 then exit 1

queue 'Alpha'
queue 'Beta'
push 'Gamma'
mixedCount = queued()
parse pull first
pull second
parse pull third
queue 'Beta'
parse upper pull upperTranslated
finalCount = queued()
if mixedCount <> 3 then exit 2
if first <> 'Gamma' then exit 3
if second <> 'ALPHA' then exit 4
if third <> 'Beta' then exit 5
if upperTranslated <> 'BETA' then exit 6
if finalCount <> 0 then exit 7
say 'RX30_QUEUE_PUSH_COUNTS_ORDER='||initialCount||'/'||mixedCount||'/'||first||'/'||second||'/'||third||'/'||upperTranslated||'/'||finalCount

queue 'MiXeD'
parse pull preserved
queue 'MiXeD'
pull translated
if preserved <> 'MiXeD' then exit 7
if translated <> 'MIXED' then exit 8
say 'RX30_CASE_PRESERVE_UPPER='||preserved||'/'||translated

push
emptyCount = queued()
parse pull emptyLine
emptyLength = length(emptyLine)
emptyFinalCount = queued()
if emptyCount <> 1 then exit 9
if emptyLength <> 0 then exit 10
if emptyFinalCount <> 0 then exit 11
say 'RX30_EMPTY_COUNT_LENGTH_FINAL='||emptyCount||'/'||emptyLength||'/'||emptyFinalCount

queue 'one two three'
parse pull left . right
if left <> 'one' then exit 12
if right <> 'three' then exit 13
say 'RX30_TEMPLATE_PLACEHOLDER='||left||'/'||right

call rx30_outer
parse pull fromRoutine
interpret "queue 'Dynamic'"
parse pull fromInterpret
if fromRoutine <> 'Routine' then exit 14
if fromInterpret <> 'Dynamic' then exit 15
say 'RX30_ROUTINE_INTERPRET='||fromRoutine||'/'||fromInterpret
say 'RX30_QUEUE_DEMO_DONE'
exit 0

rx30_outer:
call rx30_inner
return

rx30_inner:
push 'Routine'
return