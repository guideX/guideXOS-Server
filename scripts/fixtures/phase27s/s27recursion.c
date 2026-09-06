int sum_ptr(int* p, int count) { if (count == 0) { return 0; } return *p + sum_ptr(p + 1, count - 1); }
int gx_main(gx_app_context* ctx) { int values[4]; values[0] = 10; values[1] = 11; values[2] = 12; values[3] = 9; return sum_ptr(&values[0], 4); }
