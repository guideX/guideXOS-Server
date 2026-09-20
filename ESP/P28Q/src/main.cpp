extern int gx_window_create(int width, int height, char* title);
extern int gx_window_set_text(int window, char* text);
extern int gx_window_destroy(int window);
int helper(int input, int delta);

int gx_main(gx_app_context* ctx)
{
    int counter = 0;
    int accumulator = 0;
    int window = gx_window_create(320, 180, "Developer Studio Phase 28Q");
    if (window == 0) return -1;

    while (counter < 2000000)
    {
        accumulator = accumulator + helper(counter, 3);
        counter = counter + 1;
        if ((counter & 4095) == 0)
        {
            gx_window_set_text(window, "Phase 28Q running");
        }
    }

    const int expected = 1389447424;
    const int result = counter == 2000000 && accumulator == expected ? 0 : -4;
    if (gx_window_destroy(window) != 0) return -3;
    return result;
}
