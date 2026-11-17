# Ryu binary64 formatter provenance

- Upstream: [ulfjack/ryu](https://github.com/ulfjack/ryu)
- Release tag: `v2.0` (annotated tag object `6bcd775b74b112bfd4541af3b7c10cfd841cb53f`)
- Pinned commit: `72e18e8d14d4392a4a3261f37cde248e4fbedbec`
- Selected algorithm: binary64 shortest round-trip conversion (`d2s`)
- License: Boost Software License 1.0, retained in `LICENSE-Boost`; upstream
  also permits the implementation under Apache-2.0.

The formatter is included from `navigator_javascript/json_number_format.inl`
and writes to caller-owned storage. It does not allocate. The shared ECMAScript
presentation layer parses Ryu's shortest scientific digit sequence and applies
the ECMAScript fixed/exponential thresholds, lower-case `e`, positive exponent
sign, and no exponent leading zeros. JSON non-finite values map to `null` and
both zero signs map to `0`.

The pinned upstream SHA-256 values and the adapted in-tree values are:

| File | Upstream SHA-256 | In-tree SHA-256 |
|---|---|---|
| `common.h` | `0EDB75C6E769F289C7E058BDC3A98F67C54DF085AB975B8BC414464D0D17C8DD` | `02DF37108C6BF1CF3DB656B0CF728460E57EBD9E52A1F76CBD611D61B2C6E5BB` |
| `d2s.c` | `C98E19C50A9AE313A15DB01B77F6B2AE3ED3FE7FAC02F7D8954241811596259B` | `24336D13F19E841E76401D5670F731AEB03A8F50E9E738999C0268B55DDAA478` |
| `d2s.h` | `064422CC3D7F26ED5D4F14ECB1657A3B27EAC0C9C0955F1171ED0AD146F5694D` | `16B8ED4BD1651BE38D1B34BD2B4D5352328F5B8379E4ADDAF2AE91244123C8D4` |
| `d2s_full_table.h` | `B761199C60B46258506FB3B77D049D533248F1C6899FC99A7FB00F7461C53656` | `B761199C60B46258506FB3B77D049D533248F1C6899FC99A7FB00F7461C53656` |
| `d2s_intrinsics.h` | `E52C2E2B2841D64C01C5EBB830982EEF881802CC366EFEB3C779BC74C0FAD143` | `EE5F017C9C6D97597255E6A87072B771836E14AB8BDC879CF35164F4594BA0BE` |
| `digit_table.h` | `47B58C924979B533D71FF2F513D93B759DA34A7AA73A5E33BA7DF11BE91C80AA` | `47B58C924979B533D71FF2F513D93B759DA34A7AA73A5E33BA7DF11BE91C80AA` |
| `ryu.h` | `1344FBF2D48923EC9362AC24D2212492B0170B0CA2BF0DB2EFA4C1DB2AD66A1A` | `1344FBF2D48923EC9362AC24D2212492B0170B0CA2BF0DB2EFA4C1DB2AD66A1A` |

Target adaptations are limited to:

1. Local include paths replace upstream `ryu/...` paths so the source works
   from the `third_party/ryu` directory.
2. The implementation's fixed-size byte copies use `ryu_copy_bytes`, a real
   local byte loop, instead of importing `memcpy` from a C runtime.
3. Only the buffered binary64 entry point is retained. It has internal inline
   linkage; the heap-returning `d2s` and unused `d2s_buffered` wrappers are
   omitted.
4. The formatter is compiled as part of the shared header-included portable
   component. Freestanding qualification uses `-DNDEBUG` to remove debug
   assertion abort paths while preserving algorithm checks in hosted builds.

No generic `printf`, locale state, dynamic allocation, standard C++ runtime,
or floating-point helper import is used. The actual freestanding shared
serializer component and its undefined-symbol audit are recorded in the JS124
qualification report.
