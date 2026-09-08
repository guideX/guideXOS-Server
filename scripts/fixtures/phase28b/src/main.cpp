extern int gx_window_create(int width, int height, char* title);
extern int gx_window_set_text(int window, char* text);
extern int gx_window_destroy(int window);
extern int gx_window_run(int window);

int gx_main(gx_app_context* ctx)
{
    log(ctx, "DEVELOPER_STUDIO_PHASE28B_PRE_BREAKPOINT_ONCE");
    int breakpointValue = 28;
    breakpointValue = breakpointValue + 1;
    int window = gx_window_create(320, 180, "Developer Studio Phase 28B");
    if (window == 0) return -1;
    if (gx_window_set_text(window, "28B GUI 28") != 0) return -2;
    int result = gx_window_run(window);
    if (gx_window_destroy(window) != 0) return -3;
    return result + breakpointValue - 29;
}
