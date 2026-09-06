int advance(int* p) { p = p + 1; return *p; }
int gx_main(gx_app_context* ctx) { int values[2]; values[0] = 42; values[1] = 1; int* p = &values[0]; advance(p); return *p; }
