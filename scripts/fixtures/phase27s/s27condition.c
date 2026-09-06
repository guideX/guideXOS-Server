int gx_main(gx_app_context* ctx) { int values[2]; values[0] = 1; values[1] = 42; int* p = &values[0]; p = p + 1; if (*p == 42) { return 42; } return 0; }
