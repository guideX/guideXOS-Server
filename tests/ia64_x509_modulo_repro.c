/* Minimal reproducer for GCC 13.5 IA-64 reload ICE from x509.c year % 400. */

typedef unsigned int gxos_u32;

__attribute__((noinline)) gxos_u32 ia64_x509_year_mod_400(gxos_u32 year)
{
    return year % 400u;
}

__attribute__((noinline)) gxos_u32 ia64_x509_february_days(gxos_u32 year)
{
    return ((year & 3u) || (!(year % 100u) && (year % 400u))) ? 28u : 29u;
}
