int fill_values();
int sum_values();

int gx_main(gx_app_context* ctx)
{
    fill_values();
    int result = sum_values();
    log(ctx, "Indexed array execution completed.");
    return result;
}
