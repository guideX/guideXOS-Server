int gx_main(void* ctx)
{
    int i = 0;
    int total = 0;
    while (i < 8)
    {
        i = i + 1;
        if (i < 3)
        {
            continue;
        }
        total = total + 7;
    }
    return total;
}
