int gx_main(gx_app_context* ctx) {
    int x = 20;
    int y = 22;
    int z = 0;
    if ((x == 20 && y == 22) || z != 0) {
        log(ctx, "mixed logical condition matched");
        return 42;
    }
    return 0;
}
