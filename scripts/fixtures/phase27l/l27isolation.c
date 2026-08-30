int first()
{
    int value = 40;
    return value;
}

int second()
{
    int value = 2;
    return value;
}

int gx_main(gx_app_context* ctx)
{
    return first() + second();
}
