code = 'call helper 5; say result'
interpret code
exit 0

helper:
    parse arg number
    return number * 2
