int gx_main(void* ctx)
{
    int i = 0;
    int value = 42;
    while (i < 10)
    {
        i = i + 1;
        if (i == 1)
        {
            break;
        }
        value = 0;
    }
    return value;
}
