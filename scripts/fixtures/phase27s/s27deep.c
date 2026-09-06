int recurse(int* p, int count) { if (count == 0) { return 0; } return recurse(p, count - 1); }
int gx_main(gx_app_context* ctx) { int value = 1; return recurse(&value, 1000000); }
