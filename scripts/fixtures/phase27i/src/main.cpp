int gx_main(gx_app_context* ctx) {
    int x = 20;
    int y = 22;

    if (x == 20 && y == 22) {
        log(ctx, "Both conditions are true.");
        return 42;
    }

    log(ctx, "Condition failed.");
    return 0;
}
