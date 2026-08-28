int gx_main(gx_app_context* ctx) {
    int x = 41;
    if (x == 42) {
        log(ctx, "THIS MUST NOT PRINT");
    }
    return x;
}
