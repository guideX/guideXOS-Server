sum = 0
do i = 1 to 3
    call addTwice
    call returnEarly
end
say sum
say i
exit 0

addTwice:
    do j = 1 to 2
        sum = sum + i
    end
    return

returnEarly:
    do k = 1 to 3
        return
    end
