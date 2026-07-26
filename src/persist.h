#pragma once

#include "persist_format.h"
#include "runtime.h"

#if FR_FEATURE_PERSISTENCE

fr_err_t fr_persist_save(fr_runtime_t *runtime);
/* ADR 0070: the prompt's own `save`. Stores slots holding handle values as
 * nil, keeps the live ones running, and leaves an advisory diagnostic
 * naming what will not survive a reboot. Anything it cannot hold falls
 * back to fr_persist_save, so those responses are unchanged. Only for
 * calls made outside the VM -- see the comment on the definition. */
fr_err_t fr_persist_save_tolerant(fr_runtime_t *runtime);
/* Mark a volatile save rejection for notice presentation. Both save entries
 * need it: refusing to save is advice at the prompt, not a fault. */
void fr_persist_note_save_rejection(fr_runtime_t *runtime, fr_err_t err);
fr_err_t fr_persist_restore(fr_runtime_t *runtime);
fr_err_t fr_persist_wipe(fr_runtime_t *runtime);

/* Drop every L2 overlay binding from the runtime and save so NVS only retains
 * L1 records. */
fr_err_t fr_persist_wipe_user(fr_runtime_t *runtime);
/* D10 install-library implicit L1 wipe. Drops L1 from the runtime and commits
 * the post-wipe runtime before the REPL flips the session install tier. */
fr_err_t fr_persist_install_library(fr_runtime_t *runtime);
/* D3 install-library mode persists new definitions with tier tag L1 after each
 * successful overlay-apply or value-binding. */
fr_err_t fr_persist_save_full(fr_runtime_t *runtime);
/* D6 boot two-call sequence. Restore library first, then user against the
 * runtime state left by the library pass. */
fr_err_t fr_persist_restore_library(fr_runtime_t *runtime);
fr_err_t fr_persist_restore_user(fr_runtime_t *runtime);

uint16_t fr_persist_debug_last_payload_bytes(void);
/* Debug observability for tests and size checks; not a user protocol field. */
uint32_t fr_persist_debug_profile_hash(void);

#endif
