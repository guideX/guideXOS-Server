int gx_main(void* ctx)
{
    int x = 1;
    while (x < 50)
    {
        if (x == 3)
        {
            return x;
        }
        x = x + 1;
    }
    return 0;
}
