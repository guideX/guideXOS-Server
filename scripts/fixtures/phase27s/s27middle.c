int gx_main(gx_app_context* ctx) { int values[4]; int* p = &values[2]; p = p - 2; *p = 42; return values[0]; }
