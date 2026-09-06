int update(int* p);
int gx_main(gx_app_context* ctx)
{
    int value = 40;
    update(&value);
    return value;
}
