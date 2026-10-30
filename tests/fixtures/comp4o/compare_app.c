int gx_bytes_compare(unsigned char* left, unsigned char* right, int length);

unsigned char gx_comp4o_cmp_left[256];
unsigned char gx_comp4o_cmp_right[256];
int gx_comp4o_compare_results[10];

int gx_comp4o_test_compare()
{
    int index;
    int byte_index;
    int length;
    index = 0;
    while (index < 10) {
        byte_index = 0;
        while (byte_index < 8) {
            gx_comp4o_cmp_left[byte_index] = 0;
            gx_comp4o_cmp_right[byte_index] = 0;
            byte_index = byte_index + 1;
        }
        length = 8;
        if (index == 0) {
            gx_comp4o_cmp_left[2] = 1;
            gx_comp4o_cmp_right[2] = 1;
            gx_comp4o_cmp_left[3] = 128;
            gx_comp4o_cmp_right[3] = 128;
        }
        if (index == 1) gx_comp4o_cmp_right[0] = 1;
        if (index == 2) gx_comp4o_cmp_left[0] = 1;
        if (index == 3) {
            gx_comp4o_cmp_left[7] = 3;
            gx_comp4o_cmp_right[7] = 4;
        }
        if (index == 4) {
            gx_comp4o_cmp_left[0] = 128;
            gx_comp4o_cmp_right[0] = 127;
            length = 1;
        }
        if (index == 5) {
            gx_comp4o_cmp_left[0] = 255;
            gx_comp4o_cmp_right[0] = 0;
            length = 1;
        }
        if (index == 6) length = 0;
        if (index == 7) length = -1;
        if (index == 8) length = 257;
        if (index == 9) {
            gx_comp4o_cmp_left[0] = 17;
            gx_comp4o_cmp_right[0] = 17;
            length = 256;
        }
        gx_comp4o_compare_results[index] = gx_bytes_compare(
            gx_comp4o_cmp_left, gx_comp4o_cmp_right, length);
        index = index + 1;
    }
    return 0;
}
