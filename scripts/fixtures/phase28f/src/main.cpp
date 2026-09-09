extern int gx_window_create(int width, int height, char* title);
extern int gx_window_set_text(int window, char* text);
extern int gx_window_destroy(int window);
extern int gx_window_run(int window);
int helper(int value);

int gx_main(gx_app_context* ctx)
{
    log(ctx, "DEVELOPER_STUDIO_PHASE28F_PRE_BREAKPOINT_ONCE");
    int value = 27;
    value = value + 0;
    value = value + 0;
    value = helper(value);
    value = value + 10;
    value = value + 1;
    value = value + 1;
    int window = gx_window_create(320, 180, "Developer Studio Phase 28F");
    if (window == 0) return -1;
    if (gx_window_set_text(window, "28F GUI 40") != 0) return -2;
    int result = gx_window_run(window);
    if (gx_window_destroy(window) != 0) return -3;
    return result + value - 40;
}
