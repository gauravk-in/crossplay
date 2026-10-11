// FORK CHANGE: CrossPlay's additions to the wasm3 API. They live here rather
// than in the firmware because they read wasm3's internal structs, whose layout
// depends on the build flags in ../library.json, which only this library's own
// sources are compiled with.

#ifndef m3_fork_h
#define m3_fork_h

#include <stdbool.h>
#include <stdint.h>

#include "wasm3.h"

#if defined(__cplusplus)
extern "C" {
#endif

// How many imported functions have nothing linked to them. When there is at
// least one, the first one's module and field names are written out. wasm3
// only notices a missing import when the function calling it is first
// compiled, which can be long after the app started; the firmware checks here
// first and refuses to start the app instead.
uint32_t m3fork_UnlinkedImports(IM3Module i_module, const char** o_module, const char** o_field);

// Caps linear memory at `i_maxBytes` (rounded down to whole pages). memory.grow
// past the cap fails the way it does at the module's own maximum, returning -1
// to the app. False when the module already starts with more than the cap.
bool m3fork_LimitMemory(IM3Runtime i_runtime, uint32_t i_maxBytes);

#if defined(__cplusplus)
}
#endif

#endif  // m3_fork_h
