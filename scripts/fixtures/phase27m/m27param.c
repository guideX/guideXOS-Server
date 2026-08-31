int descend(int n)
{
    if (n == 0) { return 42; }
    n = n - 1;
    return descend(n);
}

int gx_main(gx_app_context* ctx)
{
    return descend(6);
}
