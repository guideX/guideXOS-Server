start = time('E')
do i = 1 to 100000
  value = i
end
finish = time('E')
say 'RX19R1_TIMER_START=' start
say 'RX19R1_TIMER_FINISH=' finish
if finish = start then do
  say 'RX19R1_TIMER_STALLED'
  exit 1
end
say 'RX19R1_ELAPSED_TIMER_PROGRESS'
