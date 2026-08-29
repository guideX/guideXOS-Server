int gx_main(gx_app_context* ctx) {
    int x = 0;
    int y = 22;
    if (x == 20 || y == 22) {
        log(ctx, "OR condition matched.");
        return 42;
    }
    return 0;
}
