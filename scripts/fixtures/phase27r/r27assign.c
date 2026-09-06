int gx_main(gx_app_context* ctx)
{
    int a = 40;
    int b = 41;
    int* p = &a;
    p = &b;
    *p = *p + 1;
    return b;
}
