int values[4];
int gx_main(gx_app_context* ctx) { values[0] = 10; values[1] = 11; values[2] = 12; values[3] = 9; int* p = values; return *p + *(p + 1) + *(p + 2) + *(p + 3); }
