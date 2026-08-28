int gx_main(gx_app_context* ctx) {
    int result = 0;
    int x = 42;
    if (x == 42) {
        result = 40;
        result = result + 2;
    } else {
        result = -1;
    }
    return result;
}
