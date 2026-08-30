int helper()
{
    return 40;
}

int gx_main(gx_app_context* ctx)
{
    return answer() + helper();
}

int answer()
{
    return 2;
}
