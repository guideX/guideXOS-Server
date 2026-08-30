int gx_main(void* ctx)
{
    int i = 0;
    while (i < 100)
    {
        i = i + 1;
        if (i == 6)
        {
            break;
        }
    }
    return i * 7;
}
