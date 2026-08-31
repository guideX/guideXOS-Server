int recurse(int n)
{
    if (n == 0) { return 42; }
    return recurse(n - 1);
}

int gx_main(gx_app_context* ctx)
{
    return recurse(89);
}
