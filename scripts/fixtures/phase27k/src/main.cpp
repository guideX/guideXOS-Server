int gx_main(gx_app_context* ctx)
{
    int i = 0;
    int total = 0;

    log(ctx, "Starting controlled loop.");

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

    log(ctx, "Controlled loop complete.");

    return total + 9;
}
