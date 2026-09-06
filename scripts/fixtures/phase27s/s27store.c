int gx_main(gx_app_context* ctx) { int values[4]; int* p = &values[0]; int i = 0; while (i < 4) { *p = i + 10; p = p + 1; i = i + 1; } return values[0] + values[1] + values[2] + values[3] - 4; }
