int gx_main(void* ctx)
{
    int i = 0;
    int total = 0;
    while (i < 10)
    {
        i = i + 1;
        if (i < 3)
        {
            continue;
        }
        if (i > 8)
        {
            break;
        }
        total = total + i;
    }
    return total + 9;
}
