int gx_main(gx_app_context* ctx)
{
    int values[2];
    values[0] = 40;
    int* p = values;
    p = p + 1;
    p = p - 1;
    *p = *p + 2;
    return values[0];
}
