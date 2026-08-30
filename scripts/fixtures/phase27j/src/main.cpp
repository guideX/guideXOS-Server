int gx_main(gx_app_context* ctx)
{
    int total = 0;
    int i = 1;

    log(ctx, "Starting loop.");

    while (i <= 6)
    {
        total = total + i;
        i = i + 1;
    }

    log(ctx, "Loop complete.");

    return total * 2;
}
