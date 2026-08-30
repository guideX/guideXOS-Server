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

int gx_main(gx_app_context* ctx)
{
    return sum_to(6) * 2;
}
