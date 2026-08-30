int gx_main(gx_app_context* ctx)
{
    int i = 0;
    while (i < 10)
    {
        i = i + 1;
        log(ctx, "iteration");
        if (i == 3)
        {
            break;
        }
    }
    return 42;
}
