int gx_main(void* ctx)
{
    int i = 0;
    int total = 0;
    while (i < 6)
    {
        i = i + 1;
        if (i < 4)
        {
            continue;
        }
        total = total + 14;
    }
    return total;
}
