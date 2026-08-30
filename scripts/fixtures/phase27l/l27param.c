int modify(int x)
{
    x = x + 1;
    return x;
}

int gx_main(gx_app_context* ctx)
{
    int value = 41;
    int result = modify(value);
    return value + result - 41;
}
