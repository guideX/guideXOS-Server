int gx_main(void* ctx)
{
    int i = 0;
    int total = 0;
    while (i < 7)
    {
        if (i < 6)
        {
            total = total + 7;
        }
        i = i + 1;
    }
    return total;
}
