# SQLReadDemo

This Native ELF SDK sample calls the public `gx_host_calls` SQL functions. It
opens the one approved immutable fixture, looks up an Int64 key, scans an
inclusive Int32 range, checks a no-match result, confirms invalid key rejection
does not change the result array, runs a valid query afterward, closes its
handle, and verifies that the closed handle is rejected.

The sample checks the complete ordered locator sequences with scalar digests
derived from the trusted fixture manifest, including all 73 point locators and
all 32 range locators. It also checks insufficient result capacity and an
exclusive range that excludes both endpoint keys. It validates the ABI version,
table size, and append-only SQL slot offsets before making SQL calls. Returned
locator rows always come from the host API; the sample does not synthesize them.

The catalog is paginated in responses of up to two entries and exposes six
indexes in identity order. `IX_Rows_Edge64` at identity 5 and `IX_Rows_Edge32`
at identity 6 support signed point lookups. The demo checks all 80 keys from
each signed index against locator pairs independently decoded from persisted
pages, including their integer extrema, negative values, zero, and positive
values. It also checks the established absent probes, wrong key types,
insufficient capacity, unsupported range operations, and successful lookups
after rejected requests. Text checks preserve binary key lengths and embedded
NUL bytes.

The current App Model accepts only the packaged
`GX_SQL_DATABASE_R3AH_MULTILEVEL` identity. The supported public operations
are K64 Int64 point lookup, K32 bounded Int32 range scan, KBool Boolean point
lookup, KText binary-safe Text point lookup, Edge64 signed Int64 point lookup,
and Edge32 signed Int32 point lookup. The sample's fixed values qualify this
fixture; the API accepts caller-supplied typed keys and bounds.

Build with the normal SDK toolchain. `GUIDEXOS_APP_OUTPUT_DIR` can point the
packaged output at a temporary or phase-specific directory. The app declares
`database.read`, following the Native ELF manifest-permission check used by
the existing host ABI.

Native ELF apps currently execute in the kernel address space. The declared
permission and checked ABI buffers are useful App Model policy, but they do
not create privilege isolation or a signed-app trust boundary.
