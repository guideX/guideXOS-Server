#include "guidexos_app.h"

int gx_main(gx_app_context* ctx)
{
    int window = gx_window_create(320, 180, "Hello World");
    if (window == 0) return -1;

    if (gx_window_set_text(window, "Hello World") != 0) {
        gx_window_destroy(window);
        return -2;
    }

    int result = gx_window_run(window);
    int closeResult = gx_window_destroy(window);
    if (closeResult != 0) return -3;
    return result;
}
