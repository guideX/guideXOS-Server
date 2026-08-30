int gx_main(void* ctx)
{
    int total = 0;
    int i = 1;
    while (i <= 6)
    {
        total = total + i;
        i = i + 1;
    }
    return total * 2;
}
