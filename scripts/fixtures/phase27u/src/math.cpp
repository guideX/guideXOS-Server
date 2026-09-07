struct Point
{
    int x;
    int y;
    int tag;
};

int point_value(struct Point* p)
{
    return p->x + p->y + p->tag;
}
