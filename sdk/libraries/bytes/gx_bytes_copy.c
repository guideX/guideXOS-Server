int gx_bytes_copy(unsigned char* destination, unsigned char* source, int length)
{
    int index;
    if (length < 0) return -1;
    if (length > 256) return -2;
    index = 0;
    while (index < length) {
        destination[index] = source[index];
        index = index + 1;
    }
    return 0;
}
