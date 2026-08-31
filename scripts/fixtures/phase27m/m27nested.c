int add_one(int x)
{
    return x + 1;
}

int count_down(int n)
{
    if (n == 0) { return 36; }
    return count_down(n - 1) + add_one(0);
}

int gx_main(gx_app_context* ctx)
{
    return count_down(6);
}
