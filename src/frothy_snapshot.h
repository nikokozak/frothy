#pragma once

#include "froth_types.h"
#include "frothy_value.h"

froth_error_t frothy_snapshot_save(void);
froth_error_t frothy_snapshot_restore(void);
froth_error_t frothy_snapshot_wipe(void);
froth_error_t frothy_builtin_save(frothy_runtime_t *runtime,
                                  const void *context,
                                  const frothy_value_t *args,
                                  size_t arg_count, frothy_value_t *out);
froth_error_t frothy_builtin_restore(frothy_runtime_t *runtime,
                                     const void *context,
                                     const frothy_value_t *args,
                                     size_t arg_count, frothy_value_t *out);
froth_error_t frothy_builtin_wipe(frothy_runtime_t *runtime,
                                  const void *context,
                                  const frothy_value_t *args,
                                  size_t arg_count, frothy_value_t *out);
