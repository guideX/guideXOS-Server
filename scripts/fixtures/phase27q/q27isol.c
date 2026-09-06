int first()
{
    int values[2];
    values[0] = 40;
    values[1] = 2;
    return values[0] + values[1];
}

int second()
{
    int values[2];
    values[0] = 20;
    values[1] = 22;
    return values[0] + values[1];
}

int gx_main(gx_app_context* ctx) { return first() + second() - 42; }
