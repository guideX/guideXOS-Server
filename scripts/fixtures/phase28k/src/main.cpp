extern int gx_window_create(int width, int height, char* title);
extern int gx_window_set_text(int window, char* text);
extern int gx_window_run(int window);
extern int gx_window_destroy(int window);
int phase28k_helper(int input);

int gx_main(gx_app_context* ctx)
{
    int first = phase28k_helper(1);
    int second = phase28k_helper(2);
    int third = phase28k_helper(3);
    int fourth = phase28k_helper(4);
    int totalFirst = first + second;
    int totalSecond = third + fourth;
    int result = totalFirst + totalSecond;
    int adjusted = result + 0;
    int finalValue = adjusted + 0;
    int window = gx_window_create(320, 120, "Phase 28K");
    if (window == 0) return -1;
    if (gx_window_set_text(window, "28K GUI 14") != 0) return -2;
    int run = gx_window_run(window);
    if (gx_window_destroy(window) != 0) return -3;
    return finalValue - 14 + run;
}
