int add(int a, int b)
{
    return a + b;
}

int double_value(int x)
{
    return x * 2;
}

int gx_main(gx_app_context* ctx)
{
    return double_value(add(19, 2));
}
