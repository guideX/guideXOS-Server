do i = 1 to 5
    do j = 1 to 5
        if i = 2 then if j = 2 then signal finished
        say i j
    end
end
say "Skipped"
finished:
say "finished"
do k = 1 to 2
    say k
end
exit 0
