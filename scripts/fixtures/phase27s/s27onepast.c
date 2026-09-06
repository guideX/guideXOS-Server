int gx_main(gx_app_context* ctx) { int values[4]; int* p = &values[0]; p = p + 4; if (p == p) { return 42; } return 0; }
