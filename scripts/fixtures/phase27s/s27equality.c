int gx_main(gx_app_context* ctx) { int values[4]; int* p = &values[0]; int* q = &values[0]; p = p + 2; q = q + 2; if (p == q) { return 42; } return 0; }
