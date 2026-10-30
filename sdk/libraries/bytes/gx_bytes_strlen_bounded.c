int gx_bytes_strlen_bounded(unsigned char* text, int max_length)
{
    int index;
    if (max_length < 0) return -2;
    if (max_length > 256) return -3;
    index = 0;
    while (index < max_length) {
        if (text[index] == 0) return index;
        index = index + 1;
    }
    return -1;
}
