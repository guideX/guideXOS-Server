int sum_down(int n)
{
    if (n <= 0) { return 0; }
    return n + sum_down(n - 1);
}

int gx_main(gx_app_context* ctx)
{
    int result = sum_down(6) * 2;
    log(ctx, "Recursive functions executed.");
    return result;
}
