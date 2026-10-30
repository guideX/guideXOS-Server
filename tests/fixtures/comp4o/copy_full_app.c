int gx_bytes_copy(unsigned char* destination, unsigned char* source, int length);

unsigned char gx_comp4o_full_source[256];
unsigned char gx_comp4o_full_copy[256];
int gx_comp4o_copy_full_result;

int gx_comp4o_test_copy_full()
{
    int index;
    index = 0;
    while (index < 256) {
        gx_comp4o_full_source[index] = index + 1;
        index = index + 1;
    }
    gx_comp4o_full_source[255] = 0;
    gx_comp4o_copy_full_result = gx_bytes_copy(gx_comp4o_full_copy,
                                                gx_comp4o_full_source, 256);
    return 0;
}
