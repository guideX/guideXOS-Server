int gx_main(gx_app_context* ctx)
{
    int i = 0;
    while (i < 5)
    {
        i = i + 1;
        if (i < 3)
        {
            continue;
        }
        log(ctx, "kept iteration");
    }
    return 42;
}
