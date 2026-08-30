int add4(int a, int b, int c, int d)
{
    return a + b + c + d;
}

int gx_main(gx_app_context* ctx)
{
    return add4(10, 11, 12, 9);
}
