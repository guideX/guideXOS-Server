#include "guidexos_app.h"

int gx_main(gx_app_context* ctx)
{
    log(ctx, "DEVELOPER_STUDIO_PHASE27Z_FIRST_USER_OPERATION_ONCE");
    int window = gx_window_create(320, 180, "Developer Studio Phase 27Z");
    if (window == 0) return -1;
    if (gx_window_set_text(window, "27Z GUI 27") != 0) return -2;
    int result = gx_window_run(window);
    if (gx_window_destroy(window) != 0) return -3;
    return result;
}
