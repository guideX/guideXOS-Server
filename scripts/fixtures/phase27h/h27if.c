int gx_main(gx_app_context* ctx) {
    int x = 42;
    if (x == 42) {
        log(ctx, "true branch");
    }
    return x;
}
