#include "froth_ffi.h"

FROTH_FFI_ARITY(project_legacy_int, "project.legacy.int", "( value -- value )",
                1, 1, "Project-legacy FFI test binding.") {
  FROTH_POP(value);
  FROTH_PUSH(value);
  return FROTH_OK;
}

const froth_ffi_entry_t froth_project_bindings[] = {
    FROTH_BIND(project_legacy_int),
    {0},
};
