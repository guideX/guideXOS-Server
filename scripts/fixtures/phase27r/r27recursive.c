int recursive(int n)
{
    int value = n;
    int* p = &value;
    if (n == 0) return *p;
    return *p + recursive(n - 1);
}

int gx_main(gx_app_context* ctx)
{
    return recursive(6) * 2;
}
