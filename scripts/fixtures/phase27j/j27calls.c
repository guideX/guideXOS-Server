int gx_main(gx_app_context* ctx)
{
    int i = 0;
    while (i < 3)
    {
        log(ctx, "loop iteration");
        i = i + 1;
    }
    return 42;
}
