#include "frothy_ffi.h"

static const frothy_ffi_param_t project_echo_int_params[] = {
    FROTHY_FFI_PARAM_INT("value"),
};

static froth_error_t project_echo_int(frothy_runtime_t *runtime,
                                      const void *context,
                                      const frothy_value_t *args,
                                      size_t arg_count, frothy_value_t *out) {
  int32_t value = 0;

  (void)runtime;
  (void)context;
  (void)arg_count;
  FROTH_TRY(frothy_ffi_expect_int(args, 0, &value));
  return frothy_ffi_return_int(value, out);
}

const frothy_ffi_entry_t frothy_project_bindings[] = {
    {
        .name = "project.echo.int",
        .params = project_echo_int_params,
        .param_count = FROTHY_FFI_PARAM_COUNT(project_echo_int_params),
        .arity = 1,
        .result_type = FROTHY_FFI_VALUE_INT,
        .help = "Project-maintained FFI test binding.",
        .flags = FROTHY_FFI_FLAG_NONE,
        .callback = project_echo_int,
        .stack_effect = "( value -- value )",
    },
    {0},
};
