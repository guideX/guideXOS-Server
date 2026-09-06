int gx_main(gx_app_context* ctx)
{
    int values[4];
    int i = 2;
    values[2] = 40;
    int* p = &values[i];
    *p = *p + 2;
    return values[2];
}
