extern int gx_window_create(int width, int height, char* title);
extern int gx_window_set_text(int window, char* text);
extern int gx_window_run(int window);
extern int gx_window_destroy(int window);

int phase28j_helper(int input, int delta)
{
    int observed = input + delta;
    return observed;
}

int gx_main(gx_app_context* ctx)
{
    int first = phase28j_helper(1, 0);
    int second = phase28j_helper(2, 0);
    int third = phase28j_helper(3, 0);
    int fourth = phase28j_helper(4, 0);
    int result = (first + second + third + fourth) * 5;
    int window = gx_window_create(320, 120, "Phase 28J");
    if (window == 0) return -1;
    if (gx_window_set_text(window, "28J GUI 50") != 0) return -2;
    int run = gx_window_run(window);
    if (gx_window_destroy(window) != 0) return -3;
    return result - 50 + run;
}
