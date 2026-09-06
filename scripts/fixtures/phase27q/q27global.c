int values[4] = {10, 11, 12, 9};

int gx_main(gx_app_context* ctx)
{
    int i = 0;
    int total = 0;
    while (i < 4) {
        total = total + values[i];
        i = i + 1;
    }
    return total;
}
