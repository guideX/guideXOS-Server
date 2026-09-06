int gx_main(gx_app_context* ctx) { int values[2]; values[0] = 40; values[1] = 2; int* p = &values[0]; int* q = p; p = p + 1; q = q + 1; q = q - 1; if (*p == 2 && *q == 40) { return 42; } return 0; }
