int gx_main(void* ctx) {
    int a = 1;
    int b = 1;
    int c = 0;
    int d = 42;
    if ((a && b) && (c || d)) {
        return 42;
    }
    return 0;
}
