extern int values[4];

int fill_values();
int sum_pointer(int* p);

int gx_main(gx_app_context* ctx)
{
    fill_values();
    int result = sum_pointer(&values[0]);
    log(ctx, "Pointer traversal completed.");
    return result;
}
