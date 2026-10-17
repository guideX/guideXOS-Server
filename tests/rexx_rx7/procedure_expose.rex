counter = 10
message = 'Original'
items.1 = 'one'
call update
say counter
say message
say items.1
call dropShared
say counter
say items.1
exit 0

update: procedure expose counter items.
    counter = counter + 1
    message = 'Local'
    items.1 = 'changed'
    return

dropShared: procedure expose counter items.
    drop counter items.
    return
