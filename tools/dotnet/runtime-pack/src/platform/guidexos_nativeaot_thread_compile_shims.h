#pragma once

// The locked NativeAOT Windows source normally receives LPVOID from the
// Windows event-trace include chain.  The replacement compile intentionally
// leaves that optional event-trace header out; this fixed-width spelling is
// only a local source-compile prerequisite and does not cross the PAL ABI.
typedef void* LPVOID;
