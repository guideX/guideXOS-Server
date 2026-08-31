int loop_recursive(int n)
{
    int i = 0;
    while (i < 1) { i = i + 1; }
    if (n == 0) { return 36; }
    return loop_recursive(n - 1) + 1;
}

int gx_main(gx_app_context* ctx)
{
    return loop_recursive(6);
}
