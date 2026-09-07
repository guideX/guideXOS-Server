#include "point.h"
int point_value(struct Point* p) { return p->x * 100 + p->y * 10 + p->tag + shared_bias(); }
