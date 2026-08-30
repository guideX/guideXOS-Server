int gx_main(void* ctx)
{
    int enabled = 1;
    int value = 0;
    if (enabled)
    {
        while (value < 42)
        {
            value = value + 7;
        }
    }
    return value;
}
