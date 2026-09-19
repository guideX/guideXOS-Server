extern int gx_window_create(int width, int height, char* title);
extern int gx_window_set_text(int window, char* text);
extern int gx_window_destroy(int window);
extern int gx_window_run(int window);
int helper(int input, int delta);

int gx_main(gx_app_context* ctx)
{
    log(ctx, "DEVELOPER_STUDIO_PHASE28O_TARGET_START");
    int root_value = 100;
    int before_call = 27;
    int call_result = helper(10, 4);
    int repeat_result = helper(10, 4);
    int final_call_result = helper(1, 2);
    int after_call = 200;
    int window = gx_window_create(320, 180, "Developer Studio Phase 28O");
    if (window == 0) return -1;
    if (gx_window_set_text(window, "28O GUI") != 0) return -2;
    int result = gx_window_run(window);
    if (gx_window_destroy(window) != 0) return -3;
    return result + call_result + repeat_result + final_call_result + root_value + before_call + after_call - 710;
}
