int gx_main(gx_app_context* ctx)
{
    int values[4];
    int i = 0;
    while (i < 4) {
        values[i] = i + 10;
        i = i + 1;
    }
    values[3] = 9;
    return values[0] + values[1] + values[2] + values[3];
}
