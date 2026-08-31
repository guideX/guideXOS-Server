int even(int n)
{
    if (n <= 0) { return 1; }
    return odd(n - 1);
}

int odd(int n)
{
    if (n <= 0) { return 0; }
    return even(n - 1);
}

int gx_main(gx_app_context* ctx)
{
    return even(6) * 42;
}
