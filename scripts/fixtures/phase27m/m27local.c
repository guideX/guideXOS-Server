int sum_copy(int n)
{
    int current = n;
    if (n <= 0) { return 0; }
    return current + sum_copy(n - 1);
}

int gx_main(gx_app_context* ctx)
{
    return sum_copy(6) * 2;
}
