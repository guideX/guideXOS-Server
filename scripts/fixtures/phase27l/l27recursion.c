int recurse(int x)
{
    return recurse(x);
}

int gx_main(gx_app_context* ctx)
{
    return recurse(42);
}
