int recurse(int x)
{
    if (x <= 0) { return 0; }
    return 1 + recurse(x - 1);
}

int gx_main(gx_app_context* ctx)
{
    return recurse(42);
}
