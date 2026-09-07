#include "point.h"
int gx_main(gx_app_context* c) { struct Point points[3]; struct Point* p; points[1].x = 2; points[1].y = 3; points[1].tag = 4; p = &points[0]; p = p + 1; return point_value(p); }
