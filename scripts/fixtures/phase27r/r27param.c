int increment(int* p)
{
    *p = *p + 1;
    return *p;
}

int gx_main(gx_app_context* ctx)
{
    int value = 41;
    increment(&value);
    return value;
}
