int gx_main(gx_app_context* ctx)
{
    int value = 40;
    int* a = &value;
    int* b = a;
    *b = *b + 2;
    return value;
}
