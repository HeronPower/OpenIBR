#ifndef DLL_SNAPSHOT_H
#define DLL_SNAPSHOT_H

#include <windows.h>

/* "Memory grab" (TB958's own term, Section 7): captures every writable PE
 * section of a loaded DLL directly, entirely host-side -- no cooperation
 * or state-vector support needed from the DLL itself. Correct here
 * because every static in this project's controller/wrapper is a plain
 * scalar or array (no heap allocation), so copying the raw section bytes
 * is a complete state capture. */

typedef struct DllSnapshot DllSnapshot; /* opaque */

/* Captures hDll's writable sections (.data/.bss; .tls is skipped -- it's
 * a per-thread template, not live state). Returns NULL on failure. */
DllSnapshot *dll_snapshot_capture(HMODULE hDll);

/* Writes a captured snapshot back into hDll's live memory. hDll must have
 * the same section layout it was captured from (e.g. another loaded copy
 * of the same DLL file). Returns 0 on success, nonzero on a layout
 * mismatch. */
int dll_snapshot_restore(const DllSnapshot *snap, HMODULE hDll);

void dll_snapshot_free(DllSnapshot *snap);

#endif /* DLL_SNAPSHOT_H */
