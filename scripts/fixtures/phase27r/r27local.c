int gx_main(gx_app_context* ctx)
{
    int value = 40;
    int* p = &value;
    *p = *p + 2;
    return value;
}
