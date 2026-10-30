int gx_bytes_fill(unsigned char* destination, int value, int length)
{
    int index;
    if (length < 0) return -1;
    if (length > 256) return -2;
    index = 0;
    while (index < length) {
        destination[index] = value;
        index = index + 1;
    }
    return 0;
}
