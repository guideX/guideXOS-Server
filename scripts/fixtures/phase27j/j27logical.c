int gx_main(void* ctx)
{
    int i = 0;
    int enabled = 1;
    while (i < 6 && enabled)
    {
        i = i + 1;
    }
    return i * 7;
}
