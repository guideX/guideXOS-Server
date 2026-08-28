int gx_main(gx_app_context* ctx) {
    int x = 10;
    int y = 32;
    int result = x + y;
    if (result >= 40) {
        if (result == 42) {
            log(ctx, "nested exact match");
            return 42;
        } else {
            return 41;
        }
    }
    return 0;
}
