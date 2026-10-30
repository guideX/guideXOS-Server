int gx_bytes_strlen_bounded(unsigned char* text, int max_length);

unsigned char gx_comp4o_string_final[4] = {'a', 'b', 'c', 0};
unsigned char gx_comp4o_string_missing[4] = {'w', 'x', 'y', 'z'};
unsigned char gx_comp4o_string_max[256];
int gx_comp4o_string_results[7];

int gx_comp4o_test_strlen()
{
    int index;
    int limit;
    unsigned char empty_text[1];
    unsigned char* scan_text;
    empty_text[0] = 0;
    gx_comp4o_string_results[0] = gx_bytes_strlen_bounded(empty_text, 1);

    index = 1;
    while (index < 7) {
        scan_text = gx_comp4o_string_final;
        if (index == 1) limit = 4;
        if (index == 2) {
            limit = 4;
            scan_text = gx_comp4o_string_missing;
        }
        if (index == 3) limit = 0;
        if (index == 4) limit = -1;
        if (index == 5) limit = 257;
        if (index == 6) limit = 256;
        if (index == 6) {
            int byte_index;
            byte_index = 0;
            while (byte_index < 256) {
                gx_comp4o_string_max[byte_index] = byte_index + 1;
                byte_index = byte_index + 1;
            }
            gx_comp4o_string_max[255] = 0;
            scan_text = gx_comp4o_string_max;
        }
        gx_comp4o_string_results[index] = gx_bytes_strlen_bounded(scan_text, limit);
        index = index + 1;
    }
    return 0;
}
