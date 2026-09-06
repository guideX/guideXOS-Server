int gx_main(gx_app_context* ctx)
{
    int values[4];
    values[0] = 40;
    int* p = &values[0];
    *p = *p + 2;
    return values[0];
}
