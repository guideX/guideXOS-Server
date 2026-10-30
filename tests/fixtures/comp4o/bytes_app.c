int gx_comp4o_test_copy();
int gx_comp4o_test_copy_local();
int gx_comp4o_test_copy_full();
int gx_comp4o_test_fill();
int gx_comp4o_test_compare();
int gx_comp4o_test_strlen();

int gx_main(gx_app_context* ctx)
{
    if (gx_comp4o_test_copy() != 0) return 80;
    if (gx_comp4o_test_copy_local() != 0) return 84;
    if (gx_comp4o_test_copy_full() != 0) return 85;
    if (gx_comp4o_test_fill() != 0) return 81;
    if (gx_comp4o_test_compare() != 0) return 82;
    if (gx_comp4o_test_strlen() != 0) return 83;
    return 42;
}
