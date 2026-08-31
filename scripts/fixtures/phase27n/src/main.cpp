int add(int a, int b);
int helper(int value);
int gx_main(gx_app_context* ctx) {
    log(ctx, "Phase 27N initial");
    return add(helper(20), 22);
}
