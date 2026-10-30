int gx_bytes_compare(unsigned char* left, unsigned char* right, int length)
{
    int index;
    int left_value;
    int right_value;
    if (length < 0) return -2;
    if (length > 256) return -2;
    index = 0;
    while (index < length) {
        left_value = left[index];
        right_value = right[index];
        if (left_value < right_value) return -1;
        if (left_value > right_value) return 1;
        index = index + 1;
    }
    return 0;
}
