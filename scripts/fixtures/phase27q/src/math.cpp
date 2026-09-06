extern int values[4];

int fill_values()
{
    int i = 0;
    while (i < 4) {
        values[i] = i + 10;
        i = i + 1;
    }
    values[3] = 9;
    return 0;
}

int sum_values()
{
    int total = 0;
    int i = 0;
    while (i < 4) {
        total = total + values[i];
        i = i + 1;
    }
    return total;
}
