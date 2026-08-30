int sum_to(int n)
{
    int total = 0;
    int i = 1;

    while (i <= n)
    {
        total = total + i;
        i = i + 1;
    }

    return total;
}

int double_value(int value)
{
    return value * 2;
}

int gx_main(gx_app_context* ctx)
{
    int result = double_value(sum_to(6));

    log(ctx, "Functions executed.");

    return result;
}
