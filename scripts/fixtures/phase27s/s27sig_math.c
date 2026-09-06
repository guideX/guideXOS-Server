int sum_pointer(int* p) { int total = 0; int i = 0; while (i < 4) { total = total + *p; p = p + 1; i = i + 1; } return total; }
