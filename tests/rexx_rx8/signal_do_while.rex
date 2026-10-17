i = 0
do while i < 5
    i = i + 1
    if i = 3 then signal after_controlled_loop
    say i
end
say "Stale controlled loop resumed"
after_controlled_loop:
say "Controlled loop terminated" i sigl
do j = 1 to 2
    say "Next loop" j
end
exit 0
