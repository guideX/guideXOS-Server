int recurse(int n)
{
    if (n <= 0) { return 0; }
    return 1 + recurse(n - 1);
}

int gx_main(gx_app_context* ctx)
{
    log(ctx, "Phase 27M recursion primary.");
    return recurse(6) * 7;
}
