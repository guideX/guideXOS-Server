int gx_main(gx_app_context* ctx) { int value = 40; int* p = &value; p = p + 1; if (p == p) { return 42; } return 0; }
