int gx_main(gx_app_context* ctx) {
    int x = 20;
    int y = 21;
    int result = x + y;
    if (result == 42) {
        log(ctx, "The answer is 42.");
        return result;
    } else {
        log(ctx, "Unexpected result.");
        return -1;
    }
}
