int gx_main(void* ctx) {
    int x = 20;
    int y = 22;
    int matched = x == 20 && y == 22;
    return matched * 42;
}
