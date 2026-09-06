int sum_pointer(int* p);
int gx_main(gx_app_context* ctx) { int values[4]; values[0] = 10; values[1] = 11; values[2] = 12; values[3] = 9; return sum_pointer(&values[0]); }
