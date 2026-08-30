int calculate()
{
    int i = 0;
    int total = 0;

    while (i < 10)
    {
        i = i + 1;

        if (i < 3)
        {
            continue;
        }

        if (i > 8)
        {
            break;
        }

        total = total + i;
    }

    return total + 9;
}

int gx_main(gx_app_context* ctx)
{
    return calculate();
}
