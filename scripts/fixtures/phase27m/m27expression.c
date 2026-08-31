int sum_down(int n)
{
    if (n <= 0) { return 0; }
    int value = sum_down(n - 1);
    return value + n;
}

int gx_main(gx_app_context* ctx)
{
    return sum_down(6) * 2;
}
