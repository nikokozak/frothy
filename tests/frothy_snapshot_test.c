#include "froth_slot_table.h"
#include "froth_snapshot.h"
#include "froth_vm.h"
#include "frothy_base_image.h"
#include "frothy_boot.h"
#include "frothy_eval.h"
#include "frothy_inspect.h"
#include "frothy_parser.h"
#include "frothy_snapshot.h"
#include "frothy_value.h"
#include "platform.h"

#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
  char original_dir[PATH_MAX];
  char path[PATH_MAX];
} temp_workspace_t;

static bool snapshot_runtime_bootstrapped = false;
static bool snapshot_platform_initialized = false;
static const uint16_t frothy_snapshot_version = 2;

extern void frothy_snapshot_test_set_error_after_objects(froth_error_t err);
extern void frothy_boot_test_set_pick_active_error(froth_error_t err);

static frothy_runtime_t *runtime(void) {
  return &froth_vm.frothy_runtime;
}

static void release_value(frothy_value_t *value) {
  (void)frothy_value_release(runtime(), *value);
  *value = frothy_value_make_nil();
}

static int bootstrap_snapshot_runtime(void) {
  if (snapshot_runtime_bootstrapped) {
    return 1;
  }

  froth_vm.heap.pointer = 0;
  froth_vm.boot_complete = 1;
  froth_vm.trampoline_depth = 0;
  froth_vm.interrupted = 0;
  froth_vm.thrown = FROTH_OK;
  froth_vm.last_error_slot = -1;
  froth_vm.mark_offset = (froth_cell_u_t)-1;
  froth_cellspace_init(&froth_vm.cellspace);
  frothy_runtime_init(runtime(), &froth_vm.cellspace);
  if (frothy_base_image_install() != FROTH_OK) {
    fprintf(stderr, "failed to install base slots\n");
    return 0;
  }
  froth_vm.watermark_heap_offset = froth_vm.heap.pointer;
  snapshot_runtime_bootstrapped = true;
  return 1;
}

static int ensure_platform_runtime(void) {
  if (snapshot_platform_initialized) {
    return 1;
  }
  if (platform_init() != FROTH_OK) {
    fprintf(stderr, "failed to initialize platform runtime\n");
    return 0;
  }
  snapshot_platform_initialized = true;
  return 1;
}

static int enter_temp_workspace(temp_workspace_t *workspace) {
  char template_path[] = "/tmp/frothy-snapshot.XXXXXX";

  if (getcwd(workspace->original_dir, sizeof(workspace->original_dir)) == NULL) {
    perror("getcwd");
    return 0;
  }
  if (mkdtemp(template_path) == NULL) {
    perror("mkdtemp");
    return 0;
  }
  if (snprintf(workspace->path, sizeof(workspace->path), "%s", template_path) >=
      (int)sizeof(workspace->path)) {
    fprintf(stderr, "workspace path too long\n");
    return 0;
  }
  if (chdir(workspace->path) != 0) {
    perror("chdir");
    return 0;
  }

  if (!bootstrap_snapshot_runtime()) {
    return 0;
  }
  if (frothy_snapshot_wipe() != FROTH_OK) {
    fprintf(stderr, "failed to wipe snapshot workspace\n");
    return 0;
  }

  froth_vm.interrupted = 0;
  froth_vm.thrown = FROTH_OK;
  froth_vm.last_error_slot = -1;
  return 1;
}

static void leave_temp_workspace(const temp_workspace_t *workspace) {
  if (workspace->original_dir[0] != '\0') {
    (void)chdir(workspace->original_dir);
  }
}

static int eval_source(const char *source, frothy_value_t *out,
                       froth_error_t *error_out) {
  frothy_ir_program_t program;
  froth_error_t err;

  frothy_ir_program_init(&program);
  err = frothy_parse_top_level(source, &program);
  if (err == FROTH_OK) {
    err = frothy_eval_program(&program, out);
  }
  frothy_ir_program_free(&program);
  *error_out = err;
  return err == FROTH_OK;
}

static int expect_ok(const char *source, frothy_value_t *out) {
  froth_error_t err;

  if (!eval_source(source, out, &err)) {
    fprintf(stderr, "eval failed for `%s`: %d\n", source, (int)err);
    return 0;
  }
  return 1;
}

static int expect_error(const char *source, froth_error_t expected) {
  frothy_value_t ignored = frothy_value_make_nil();
  froth_error_t err;

  if (eval_source(source, &ignored, &err)) {
    fprintf(stderr, "expected error %d for `%s`\n", (int)expected, source);
    release_value(&ignored);
    return 0;
  }
  if (err != expected) {
    fprintf(stderr, "expected error %d for `%s`, got %d\n", (int)expected,
            source, (int)err);
    return 0;
  }
  return 1;
}

static int expect_int_value(frothy_value_t value, int32_t expected,
                            const char *label) {
  if (!frothy_value_is_int(value) || frothy_value_as_int(value) != expected) {
    fprintf(stderr, "%s expected int %d\n", label, expected);
    return 0;
  }
  return 1;
}

static int expect_nil_value(frothy_value_t value, const char *label) {
  if (!frothy_value_is_nil(value)) {
    fprintf(stderr, "%s expected nil\n", label);
    return 0;
  }
  return 1;
}

static int expect_text_value(frothy_value_t value, const char *expected,
                             const char *label) {
  const char *text = NULL;
  size_t length = 0;

  if (frothy_runtime_get_text(runtime(), value, &text, &length) != FROTH_OK) {
    fprintf(stderr, "%s expected text\n", label);
    return 0;
  }
  if (strlen(expected) != length || memcmp(text, expected, length) != 0) {
    fprintf(stderr, "%s expected `%s`, got `%.*s`\n", label, expected,
            (int)length, text);
    return 0;
  }
  return 1;
}

static int expect_live_objects(size_t expected, const char *label) {
  size_t live = frothy_runtime_live_object_count(runtime());

  if (live != expected) {
    fprintf(stderr, "%s expected %zu live objects, got %zu\n", label, expected,
            live);
    return 0;
  }
  return 1;
}

static int expect_payload_used(size_t expected, const char *label) {
  size_t used = frothy_runtime_payload_used(runtime());

  if (used != expected) {
    fprintf(stderr, "%s expected payload used %zu, got %zu\n", label, expected,
            used);
    return 0;
  }
  return 1;
}

static int expect_text_equal(const char *actual, const char *expected,
                             const char *label) {
  if (strcmp(actual, expected) != 0) {
    fprintf(stderr, "%s expected `%s`, got `%s`\n", label, expected, actual);
    return 0;
  }
  return 1;
}

static int capture_code_renders(const char *name, char **see_out,
                                char **core_out) {
  frothy_inspect_binding_view_t view = {0};

  *see_out = NULL;
  *core_out = NULL;
  if (frothy_inspect_render_binding_view(runtime(), name, &view) != FROTH_OK) {
    fprintf(stderr, "failed to render see view for `%s`\n", name);
    return 0;
  }
  *see_out = view.rendered;
  view.rendered = NULL;
  frothy_inspect_binding_view_free(&view);

  if (frothy_inspect_render_binding_text(runtime(), name,
                                         FROTHY_INSPECT_RENDER_CORE,
                                         core_out) !=
      FROTH_OK) {
    fprintf(stderr, "failed to render core view for `%s`\n", name);
    free(*see_out);
    *see_out = NULL;
    return 0;
  }

  return 1;
}

static int capture_report_text(const char *name,
                               frothy_inspect_report_mode_t mode,
                               char **report_out) {
  *report_out = NULL;
  if (frothy_inspect_render_binding_report(runtime(), name, mode, report_out) !=
      FROTH_OK) {
    fprintf(stderr, "failed to render inspect report for `%s`\n", name);
    return 0;
  }

  return 1;
}

static int expect_binding_view(const char *name, bool expected_overlay,
                               frothy_value_class_t expected_class,
                               const char *label) {
  frothy_inspect_binding_view_t view = {0};
  int ok = 1;

  if (frothy_inspect_render_binding_view(runtime(), name, &view) != FROTH_OK) {
    fprintf(stderr, "%s failed to inspect `%s`\n", label, name);
    return 0;
  }
  if (view.is_overlay != expected_overlay || view.value_class != expected_class) {
    fprintf(stderr, "%s expected `%s` overlay=%d class=%s, got overlay=%d class=%s\n",
            label, name, expected_overlay ? 1 : 0,
            frothy_inspect_class_name(expected_class),
            view.is_overlay ? 1 : 0,
            frothy_inspect_class_name(view.value_class));
    ok = 0;
  }
  frothy_inspect_binding_view_free(&view);
  return ok;
}

static int expect_binding_render_view(const char *name,
                               frothy_value_class_t expected_class,
                               const char *expected_class_name,
                               const char *expected_render,
                               const char *label) {
  frothy_inspect_binding_view_t view = {0};
  int ok = 1;

  if (frothy_inspect_render_binding_view(runtime(), name, &view) != FROTH_OK) {
    fprintf(stderr, "%s failed to render binding view for `%s`\n", label, name);
    return 0;
  }
  if (view.value_class != expected_class) {
    fprintf(stderr, "%s expected class %d for `%s`, got %d\n", label,
            (int)expected_class, name, (int)view.value_class);
    ok = 0;
  }
  if (strcmp(frothy_inspect_class_name(view.value_class), expected_class_name) !=
      0) {
    fprintf(stderr, "%s expected class name `%s` for `%s`, got `%s`\n", label,
            expected_class_name, name,
            frothy_inspect_class_name(view.value_class));
    ok = 0;
  }
  if (strcmp(view.rendered, expected_render) != 0) {
    fprintf(stderr, "%s expected render `%s`, got `%s`\n", label,
            expected_render, view.rendered);
    ok = 0;
  }
  frothy_inspect_binding_view_free(&view);
  return ok;
}

static int expect_snapshot_present(bool expected, const char *label) {
  uint8_t slot = 0;
  uint32_t generation = 0;
  froth_error_t err = froth_snapshot_pick_active(&slot, &generation);

  if (expected && err != FROTH_OK) {
    fprintf(stderr, "%s expected active snapshot, got %d\n", label, (int)err);
    return 0;
  }
  if (!expected && err != FROTH_ERROR_SNAPSHOT_NO_SNAPSHOT) {
    fprintf(stderr, "%s expected no snapshot, got %d\n", label, (int)err);
    return 0;
  }
  return 1;
}

static int patch_active_snapshot_payload(size_t payload_offset,
                                         const void *replacement,
                                         size_t length, bool refresh_header) {
  uint8_t slot = 0;
  uint32_t generation = 0;
  const char *path;
  FILE *file = NULL;
  uint8_t header[FROTH_SNAPSHOT_HEADER_SIZE];
  froth_snapshot_header_info_t info;
  uint8_t *payload = NULL;
  int ok = 0;

  if (froth_snapshot_pick_active(&slot, &generation) != FROTH_OK) {
    fprintf(stderr, "no active snapshot to patch\n");
    return 0;
  }

  path = slot == 0 ? FROTH_SNAPSHOT_PATH_A : FROTH_SNAPSHOT_PATH_B;
  file = fopen(path, "r+b");
  if (file == NULL) {
    perror("fopen");
    return 0;
  }

  if (fseek(file, 0, SEEK_SET) != 0 ||
      fread(header, 1, sizeof(header), file) != sizeof(header)) {
    perror("fread");
    goto done;
  }
  if (froth_snapshot_parse_header(header, &info) != FROTH_OK) {
    fprintf(stderr, "failed to parse snapshot header for patch\n");
    goto done;
  }
  if (payload_offset + length > info.payload_len) {
    fprintf(stderr, "patch offset out of range\n");
    goto done;
  }

  if (fseek(file, (long)(FROTH_SNAPSHOT_HEADER_SIZE + payload_offset), SEEK_SET) !=
          0 ||
      fwrite(replacement, 1, length, file) != length) {
    perror("fwrite");
    goto done;
  }

  if (refresh_header) {
    payload = (uint8_t *)malloc(info.payload_len);
    if (payload == NULL) {
      fprintf(stderr, "failed to allocate payload patch buffer\n");
      goto done;
    }
    if (fseek(file, FROTH_SNAPSHOT_HEADER_SIZE, SEEK_SET) != 0 ||
        fread(payload, 1, info.payload_len, file) != info.payload_len) {
      perror("fread");
      goto done;
    }
    if (froth_snapshot_build_header(header, info.payload_len, payload,
                                    info.generation) != FROTH_OK) {
      fprintf(stderr, "failed to rebuild snapshot header\n");
      goto done;
    }
    if (fseek(file, 0, SEEK_SET) != 0 ||
        fwrite(header, 1, sizeof(header), file) != sizeof(header)) {
      perror("fwrite");
      goto done;
    }
  }

  ok = 1;

done:
  free(payload);
  fclose(file);
  return ok;
}

typedef struct {
  size_t record_def_name_offset;
  size_t record_field_count_offset;
  bool found_record_def_name;
  bool found_record_field_count;
} record_snapshot_offsets_t;

static int load_active_snapshot_payload(uint8_t **payload_out,
                                        uint32_t *payload_length_out) {
  uint8_t slot = 0;
  uint32_t generation = 0;
  const char *path;
  FILE *file = NULL;
  uint8_t header[FROTH_SNAPSHOT_HEADER_SIZE];
  froth_snapshot_header_info_t info;
  uint8_t *payload = NULL;
  int ok = 0;

  *payload_out = NULL;
  *payload_length_out = 0;
  if (froth_snapshot_pick_active(&slot, &generation) != FROTH_OK) {
    fprintf(stderr, "no active snapshot to load\n");
    return 0;
  }

  path = slot == 0 ? FROTH_SNAPSHOT_PATH_A : FROTH_SNAPSHOT_PATH_B;
  file = fopen(path, "rb");
  if (file == NULL) {
    perror("fopen");
    return 0;
  }
  if (fread(header, 1, sizeof(header), file) != sizeof(header)) {
    perror("fread");
    goto done;
  }
  if (froth_snapshot_parse_header(header, &info) != FROTH_OK) {
    fprintf(stderr, "failed to parse snapshot header for load\n");
    goto done;
  }

  payload = (uint8_t *)malloc(info.payload_len);
  if (payload == NULL) {
    fprintf(stderr, "failed to allocate payload load buffer\n");
    goto done;
  }
  if (fread(payload, 1, info.payload_len, file) != info.payload_len) {
    perror("fread");
    goto done;
  }

  *payload_out = payload;
  *payload_length_out = info.payload_len;
  payload = NULL;
  ok = 1;

done:
  free(payload);
  fclose(file);
  return ok;
}

static int payload_read_u8(const uint8_t *payload, size_t payload_length,
                           size_t *offset_io, uint8_t *out) {
  if (*offset_io + 1 > payload_length) {
    return 0;
  }
  *out = payload[*offset_io];
  *offset_io += 1;
  return 1;
}

static int payload_read_u16(const uint8_t *payload, size_t payload_length,
                            size_t *offset_io, uint16_t *out) {
  if (*offset_io + 2 > payload_length) {
    return 0;
  }
  *out = (uint16_t)payload[*offset_io] |
         ((uint16_t)payload[*offset_io + 1] << 8);
  *offset_io += 2;
  return 1;
}

static int payload_read_u32(const uint8_t *payload, size_t payload_length,
                            size_t *offset_io, uint32_t *out) {
  if (*offset_io + 4 > payload_length) {
    return 0;
  }
  *out = (uint32_t)payload[*offset_io] |
         ((uint32_t)payload[*offset_io + 1] << 8) |
         ((uint32_t)payload[*offset_io + 2] << 16) |
         ((uint32_t)payload[*offset_io + 3] << 24);
  *offset_io += 4;
  return 1;
}

static int payload_skip_bytes(size_t payload_length, size_t *offset_io,
                              size_t length) {
  if (*offset_io + length > payload_length) {
    return 0;
  }
  *offset_io += length;
  return 1;
}

static int payload_skip_snapshot_value(const uint8_t *payload,
                                       size_t payload_length,
                                       size_t *offset_io) {
  uint8_t tag = 0;
  uint32_t ignored = 0;

  if (!payload_read_u8(payload, payload_length, offset_io, &tag)) {
    return 0;
  }
  switch (tag) {
  case 0:
  case 1:
  case 2:
    return 1;
  case 3:
  case 4:
    return payload_read_u32(payload, payload_length, offset_io, &ignored);
  default:
    return 0;
  }
}

static int locate_record_snapshot_offsets(record_snapshot_offsets_t *out) {
  uint8_t *payload = NULL;
  uint32_t payload_length = 0;
  size_t offset = 0;
  uint32_t symbol_count = 0;
  uint32_t object_count = 0;
  uint32_t count = 0;
  uint32_t i;
  int ok = 0;

  memset(out, 0, sizeof(*out));
  if (!load_active_snapshot_payload(&payload, &payload_length)) {
    return 0;
  }

  if (!payload_skip_bytes(payload_length, &offset, 8) ||
      !payload_read_u32(payload, payload_length, &offset, &symbol_count)) {
    goto done;
  }
  for (i = 0; i < symbol_count; i++) {
    uint16_t length = 0;

    if (!payload_read_u16(payload, payload_length, &offset, &length) ||
        !payload_skip_bytes(payload_length, &offset, length)) {
      goto done;
    }
  }

  if (!payload_read_u32(payload, payload_length, &offset, &object_count)) {
    goto done;
  }
  for (i = 0; i < object_count; i++) {
    uint8_t kind = 0;

    if (!payload_read_u8(payload, payload_length, &offset, &kind)) {
      goto done;
    }
    switch ((frothy_object_kind_t)kind) {
    case FROTHY_OBJECT_TEXT:
      if (!payload_read_u32(payload, payload_length, &offset, &count) ||
          !payload_skip_bytes(payload_length, &offset, count)) {
        goto done;
      }
      break;
    case FROTHY_OBJECT_CELLS:
      if (!payload_read_u32(payload, payload_length, &offset, &count)) {
        goto done;
      }
      while (count-- > 0) {
        if (!payload_skip_snapshot_value(payload, payload_length, &offset)) {
          goto done;
        }
      }
      break;
    case FROTHY_OBJECT_RECORD_DEF: {
      uint16_t length = 0;

      if (!payload_read_u32(payload, payload_length, &offset, &count) ||
          !payload_read_u16(payload, payload_length, &offset, &length)) {
        goto done;
      }
      out->record_def_name_offset = offset;
      out->found_record_def_name = true;
      if (!payload_skip_bytes(payload_length, &offset, length)) {
        goto done;
      }
      while (count-- > 0) {
        if (!payload_read_u16(payload, payload_length, &offset, &length) ||
            !payload_skip_bytes(payload_length, &offset, length)) {
          goto done;
        }
      }
      break;
    }
    case FROTHY_OBJECT_RECORD:
      if (!payload_skip_bytes(payload_length, &offset, 4)) {
        goto done;
      }
      out->record_field_count_offset = offset;
      out->found_record_field_count = true;
      if (!payload_read_u32(payload, payload_length, &offset, &count)) {
        goto done;
      }
      while (count-- > 0) {
        if (!payload_skip_snapshot_value(payload, payload_length, &offset)) {
          goto done;
        }
      }
      break;
    case FROTHY_OBJECT_CODE:
    case FROTHY_OBJECT_NATIVE:
    case FROTHY_OBJECT_FREE:
    default:
      fprintf(stderr, "unexpected object kind %u in record snapshot helper\n",
              (unsigned)kind);
      goto done;
    }
  }

  ok = out->found_record_def_name && out->found_record_field_count;

done:
  if (!ok) {
    fprintf(stderr, "failed to locate record snapshot offsets\n");
  }
  free(payload);
  return ok;
}

static int write_simple_text_snapshot(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  ok &= expect_ok("x is \"a\"", &value);
  release_value(&value);
  ok &= expect_ok("save:", &value);
  ok &= expect_nil_value(value, "save:");
  release_value(&value);
  return ok;
}

static int write_simple_record_snapshot(void) {
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  ok &= expect_ok("record Point [ x, y ]", &value);
  release_value(&value);
  ok &= expect_ok("point is Point: 10, 20", &value);
  release_value(&value);
  ok &= expect_ok("save:", &value);
  ok &= expect_nil_value(value, "save: record snapshot");
  release_value(&value);
  return ok;
}

static int build_overlay_text_name(size_t index, char *buffer, size_t capacity) {
  return snprintf(buffer, capacity, "t%03zu", index) < (int)capacity;
}

static int build_overlay_text_value(size_t index, char *buffer, size_t capacity) {
  return snprintf(buffer, capacity, "v%03zu", index) < (int)capacity;
}

static int bind_overlay_text_direct(const char *name, const char *text) {
  frothy_value_t value = frothy_value_make_nil();
  froth_cell_u_t slot_index = 0;

  if (frothy_runtime_alloc_text(runtime(), text, strlen(text), &value) !=
      FROTH_OK) {
    fprintf(stderr, "failed to allocate text for `%s`\n", name);
    return 0;
  }
  if (froth_slot_find_name_or_create(&froth_vm.heap, name, &slot_index) !=
          FROTH_OK ||
      froth_slot_set_overlay(slot_index, 1) != FROTH_OK ||
      froth_slot_set_impl(slot_index, frothy_value_to_cell(value)) !=
          FROTH_OK ||
      froth_slot_clear_arity(slot_index) != FROTH_OK) {
    fprintf(stderr, "failed to bind text slot `%s`\n", name);
    release_value(&value);
    return 0;
  }

  return 1;
}

static size_t near_capacity_overlay_count(void) {
  size_t slot_headroom = 0;
  size_t object_headroom = 0;
  size_t payload_headroom = 0;
  size_t count;
  const size_t bytes_per_text_binding = 24;
  const size_t bytes_per_code_binding = 40;
  const size_t payload_prefix_bytes = 20;

  if (froth_slot_count() < FROTH_SLOT_TABLE_SIZE) {
    slot_headroom = FROTH_SLOT_TABLE_SIZE - (size_t)froth_slot_count();
  }
  if (frothy_runtime_live_object_count(runtime()) < FROTHY_OBJECT_CAPACITY) {
    object_headroom =
        FROTHY_OBJECT_CAPACITY - frothy_runtime_live_object_count(runtime());
  }
  if (FROTH_SNAPSHOT_MAX_PAYLOAD_BYTES >
      payload_prefix_bytes + bytes_per_code_binding) {
    payload_headroom =
        1 + ((FROTH_SNAPSHOT_MAX_PAYLOAD_BYTES - payload_prefix_bytes -
              bytes_per_code_binding) /
             bytes_per_text_binding);
  }
  count = slot_headroom < object_headroom ? slot_headroom : object_headroom;
  if (payload_headroom < count) {
    count = payload_headroom;
  }
  if (count > 8) {
    count -= 8;
  } else if (count > 0) {
    count = 1;
  }

  return count;
}

static int populate_near_capacity_overlay(size_t count) {
  frothy_value_t value = frothy_value_make_nil();
  size_t text_count = count > 1 ? count - 1 : 0;
  size_t i;

  for (i = 0; i < text_count; i++) {
    char name[32];
    char text[48];

    if (!build_overlay_text_name(i, name, sizeof(name)) ||
        !build_overlay_text_value(i, text, sizeof(text)) ||
        !bind_overlay_text_direct(name, text)) {
      return 0;
    }
  }

  if (count > 0) {
    if (!expect_ok("nearCapCode is fn [ 7 ]", &value)) {
      return 0;
    }
    release_value(&value);
  }

  return 1;
}

static int expect_overlay_text_slot(size_t index, const char *label) {
  char name[32];
  char expected[48];
  frothy_value_t value = frothy_value_make_nil();
  int ok;

  if (!build_overlay_text_name(index, name, sizeof(name)) ||
      !build_overlay_text_value(index, expected, sizeof(expected))) {
    fprintf(stderr, "failed to build overlay text expectation for %zu\n", index);
    return 0;
  }

  ok = expect_ok(name, &value);
  if (ok) {
    ok = expect_text_value(value, expected, label);
    release_value(&value);
  }
  return ok;
}

static int prepare_startup_state(void) {
  if (frothy_base_image_reset() != FROTH_OK) {
    fprintf(stderr, "frothy_base_image_reset failed\n");
    return 0;
  }

  froth_vm.interrupted = 0;
  froth_vm.thrown = FROTH_OK;
  froth_vm.last_error_slot = -1;
  return 1;
}

static int expect_startup_report(const frothy_startup_report_t *report,
                                 bool snapshot_found,
                                 froth_error_t restore_error,
                                 bool boot_attempted,
                                 froth_error_t boot_error,
                                 const char *label) {
  if (report->snapshot_found != snapshot_found ||
      report->restore_error != restore_error ||
      report->boot_attempted != boot_attempted ||
      report->boot_error != boot_error) {
    fprintf(stderr,
            "%s expected startup report found=%d restore=%d attempted=%d "
            "boot=%d, got found=%d restore=%d attempted=%d boot=%d\n",
            label, snapshot_found ? 1 : 0, (int)restore_error,
            boot_attempted ? 1 : 0, (int)boot_error,
            report->snapshot_found ? 1 : 0, (int)report->restore_error,
            report->boot_attempted ? 1 : 0, (int)report->boot_error);
    return 0;
  }

  return 1;
}

static int test_native_dispatch_and_roundtrip(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  char *see_before = NULL;
  char *core_before = NULL;
  char *see_after = NULL;
  char *core_after = NULL;
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_snapshot_present(false, "fresh workspace");
  ok &= expect_ok("save", &value);
  release_value(&value);
  ok &= expect_error("save: 1", FROTH_ERROR_SIGNATURE);
  ok &= expect_ok("save:", &value);
  ok &= expect_nil_value(value, "save:");
  release_value(&value);
  ok &= expect_snapshot_present(true, "snapshot written");

  ok &= expect_ok("frame is cells(2)", &value);
  release_value(&value);
  ok &= expect_ok("alias is frame", &value);
  release_value(&value);
  ok &= expect_ok("label is \"ready\"", &value);
  release_value(&value);
  ok &= expect_ok("writeFrame is fn [ set frame[0] to 9; set frame[1] to \"ok\" ]",
                  &value);
  release_value(&value);
  ok &= expect_ok("writeFrame:", &value);
  release_value(&value);
  ok &= expect_ok("setAlias is fn with v [ set alias[0] to v ]", &value);
  release_value(&value);
  ok &= expect_ok("adder is fn with x [ x + alias[0] ]", &value);
  release_value(&value);
  ok &= capture_code_renders("adder", &see_before, &core_before);
  ok &= expect_ok("save:", &value);
  ok &= expect_nil_value(value, "save:");
  release_value(&value);

  ok &= expect_ok("label is \"mutated\"", &value);
  release_value(&value);
  ok &= expect_ok("setFrame is fn with v [ set frame[0] to v ]", &value);
  release_value(&value);
  ok &= expect_ok("setFrame: 1", &value);
  release_value(&value);
  ok &= expect_ok("restore:", &value);
  ok &= expect_nil_value(value, "restore:");
  release_value(&value);

  ok &= expect_ok("label", &value);
  ok &= expect_text_value(value, "ready", "label after restore");
  release_value(&value);
  ok &= expect_ok("adder: 1", &value);
  ok &= expect_int_value(value, 10, "adder: 1 after restore");
  release_value(&value);
  ok &= capture_code_renders("adder", &see_after, &core_after);
  if (ok) {
    ok &= expect_text_equal(see_after, see_before, "see render after restore");
    ok &= expect_text_equal(core_after, core_before, "core render after restore");
  }
  ok &= expect_ok("setAlias: 8", &value);
  release_value(&value);
  ok &= expect_ok("frame[0]", &value);
  ok &= expect_int_value(value, 8, "shared cells alias");
  release_value(&value);

  free(see_before);
  free(core_before);
  free(see_after);
  free(core_after);
  leave_temp_workspace(&workspace);
  return ok;
}

static int test_readability_snapshot_roundtrip(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  char *see_before = NULL;
  char *core_before = NULL;
  char *see_after = NULL;
  char *core_after = NULL;
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_ok("count is 0", &value);
  release_value(&value);
  ok &= expect_ok("mode is \"on\"", &value);
  release_value(&value);
  ok &= expect_ok("board is 5", &value);
  release_value(&value);
  ok &= expect_ok(
      "in led [ pin is 13; to route [ set @count to count + 1; cond [ when pin == 13 [ case mode [ \"on\" [ board ]; else [ nil ] ] ]; else [ nil ] ] ] ]",
      &value);
  ok &= expect_nil_value(value, "define readability snapshot code");
  release_value(&value);

  ok &= capture_code_renders("led.route", &see_before, &core_before);
  ok &= expect_ok("save:", &value);
  ok &= expect_nil_value(value, "save: readability roundtrip");
  release_value(&value);

  ok &= expect_ok("mode is \"off\"", &value);
  release_value(&value);
  ok &= expect_ok("board is 0", &value);
  release_value(&value);
  ok &= expect_ok("count is 9", &value);
  release_value(&value);

  ok &= expect_ok("restore:", &value);
  ok &= expect_nil_value(value, "restore: readability roundtrip");
  release_value(&value);

  ok &= expect_ok("led.route:", &value);
  ok &= expect_int_value(value, 5, "led.route after restore");
  release_value(&value);
  ok &= expect_ok("count", &value);
  ok &= expect_int_value(value, 1, "count after restored @count write");
  release_value(&value);

  ok &= capture_code_renders("led.route", &see_after, &core_after);
  if (ok) {
    ok &= expect_text_equal(see_after, see_before,
                            "readability see render after restore");
    ok &= expect_text_equal(core_after, core_before,
                            "readability core render after restore");
  }
  ok &= expect_ok("see: @count", &value);
  ok &= expect_nil_value(value, "see: @count after restore");
  release_value(&value);
  ok &= expect_ok("core: @count", &value);
  ok &= expect_nil_value(value, "core: @count after restore");
  release_value(&value);
  ok &= expect_ok("slotInfo: @count", &value);
  ok &= expect_nil_value(value, "slotInfo: @count after restore");
  release_value(&value);

  free(see_before);
  free(core_before);
  free(see_after);
  free(core_after);
  leave_temp_workspace(&workspace);
  return ok;
}

static int test_overlay_reset_semantics(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  size_t base_live_objects = 0;
  size_t base_payload = 0;
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  base_live_objects = frothy_runtime_live_object_count(runtime());
  base_payload = frothy_runtime_payload_used(runtime());
  ok &= expect_live_objects(base_live_objects, "base live objects");
  ok &= expect_ok("save is 1", &value);
  release_value(&value);
  ok &= expect_ok("note is \"hello\"", &value);
  release_value(&value);
  ok &= expect_ok("frame is cells(1)", &value);
  release_value(&value);
  ok &= expect_ok("touchFrame is fn [ set frame[0] to \"cell\" ]", &value);
  release_value(&value);
  ok &= expect_ok("touchFrame:", &value);
  release_value(&value);
  ok &= expect_live_objects(base_live_objects + 3, "overlay objects before reset");

  if (frothy_base_image_reset() != FROTH_OK) {
    fprintf(stderr, "frothy_base_image_reset failed\n");
    ok = 0;
  }

  ok &= expect_live_objects(base_live_objects, "base live objects after reset");
  ok &= expect_payload_used(base_payload, "base payload after reset");
  ok &= expect_error("note", FROTH_ERROR_UNDEFINED_WORD);
  ok &= expect_error("frame", FROTH_ERROR_UNDEFINED_WORD);
  ok &= expect_ok("save:", &value);
  ok &= expect_nil_value(value, "save: after reset");
  release_value(&value);
  if (froth_vm.cellspace.used != 0) {
    fprintf(stderr, "cellspace should be reset to base, got %" FROTH_CELL_U_FORMAT
                    "\n",
            froth_vm.cellspace.used);
    ok = 0;
  }

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_restore_without_snapshot_resets_to_base(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_ok("junk is 1", &value);
  release_value(&value);
  ok &= expect_error("restore:", FROTH_ERROR_SNAPSHOT_NO_SNAPSHOT);
  ok &= expect_error("junk", FROTH_ERROR_UNDEFINED_WORD);
  ok &= expect_ok("save:", &value);
  ok &= expect_nil_value(value, "save: after no-snapshot restore");
  release_value(&value);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_corrupt_snapshot_failures_reset_to_base(void) {
  static const size_t payload_version_offset = 4;
  static const size_t symbol_name_offset = 14;
  static const size_t binding_count_offset = 25;
  static const size_t binding_object_index_offset = 34;
  static const size_t text_payload_offset = 24;

  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  uint16_t bad_version = frothy_snapshot_version + 1;
  uint32_t bad_versions =
      ((uint32_t)bad_version << 16) | (uint32_t)bad_version;
  uint32_t bad_binding_count = 2;
  uint32_t bad_object_index = 99;
  uint8_t bad_name_byte = '\0';
  uint8_t bad_crc_byte = 'b';
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= write_simple_text_snapshot();
  ok &= patch_active_snapshot_payload(payload_version_offset, &bad_versions,
                                      sizeof(bad_versions), true);
  ok &= expect_ok("junk is 1", &value);
  release_value(&value);
  ok &= expect_error("restore:", FROTH_ERROR_SNAPSHOT_INCOMPAT);
  ok &= expect_error("junk", FROTH_ERROR_UNDEFINED_WORD);
  ok &= expect_ok("save:", &value);
  release_value(&value);

  ok &= frothy_snapshot_wipe() == FROTH_OK;
  ok &= write_simple_text_snapshot();
  ok &= patch_active_snapshot_payload(binding_count_offset, &bad_binding_count,
                                      sizeof(bad_binding_count), true);
  ok &= expect_ok("junk is 1", &value);
  release_value(&value);
  ok &= expect_error("restore:", FROTH_ERROR_SNAPSHOT_OVERFLOW);
  ok &= expect_error("junk", FROTH_ERROR_UNDEFINED_WORD);

  ok &= frothy_snapshot_wipe() == FROTH_OK;
  ok &= write_simple_text_snapshot();
  ok &= patch_active_snapshot_payload(binding_object_index_offset,
                                      &bad_object_index,
                                      sizeof(bad_object_index), true);
  ok &= expect_ok("junk is 1", &value);
  release_value(&value);
  ok &= expect_error("restore:", FROTH_ERROR_SNAPSHOT_FORMAT);
  ok &= expect_error("junk", FROTH_ERROR_UNDEFINED_WORD);

  ok &= frothy_snapshot_wipe() == FROTH_OK;
  ok &= write_simple_text_snapshot();
  ok &= patch_active_snapshot_payload(symbol_name_offset, &bad_name_byte,
                                      sizeof(bad_name_byte), true);
  ok &= expect_ok("junk is 1", &value);
  release_value(&value);
  ok &= expect_error("restore:", FROTH_ERROR_SNAPSHOT_BAD_NAME);
  ok &= expect_error("junk", FROTH_ERROR_UNDEFINED_WORD);

  ok &= frothy_snapshot_wipe() == FROTH_OK;
  ok &= write_simple_text_snapshot();
  ok &= patch_active_snapshot_payload(text_payload_offset, &bad_crc_byte,
                                      sizeof(bad_crc_byte), false);
  ok &= expect_ok("junk is 1", &value);
  release_value(&value);
  ok &= expect_error("restore:", FROTH_ERROR_SNAPSHOT_BAD_CRC);
  ok &= expect_error("junk", FROTH_ERROR_UNDEFINED_WORD);
  ok &= expect_ok("save:", &value);
  ok &= expect_nil_value(value, "save: after bad crc");
  release_value(&value);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_repeated_near_capacity_save_restore(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  size_t base_live_objects = 0;
  size_t overlay_count = 0;
  size_t text_count = 0;
  size_t middle_index = 0;
  size_t last_index = 0;
  char mutate_command[96];
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }
  base_live_objects = frothy_runtime_live_object_count(runtime());
  overlay_count = near_capacity_overlay_count();
  if (overlay_count == 0) {
    fprintf(stderr, "expected near-capacity overlay headroom\n");
    leave_temp_workspace(&workspace);
    return 0;
  }
  text_count = overlay_count > 1 ? overlay_count - 1 : 0;
  if (text_count > 0) {
    middle_index = text_count / 2;
    last_index = text_count - 1;
  }
  if (!populate_near_capacity_overlay(overlay_count)) {
    leave_temp_workspace(&workspace);
    return 0;
  }

  ok &= expect_live_objects(base_live_objects + overlay_count,
                            "near-cap overlay before save");
  ok &= expect_ok("save:", &value);
  ok &= expect_nil_value(value, "save: with near-cap overlay");
  release_value(&value);

  if (text_count == 0) {
    ok &= expect_ok("nearCapCode is fn [ 8 ]", &value);
  } else {
    ok &= expect_ok("t000 is \"m\"", &value);
  }
  release_value(&value);
  ok &= expect_ok("restore:", &value);
  ok &= expect_nil_value(value, "restore: with near-cap overlay");
  release_value(&value);

  ok &= expect_live_objects(base_live_objects + overlay_count,
                            "near-cap overlay after first restore");
  if (text_count > 0) {
    ok &= expect_overlay_text_slot(0, "first near-cap binding after restore");
    ok &= expect_overlay_text_slot(middle_index,
                                   "middle near-cap binding after restore");
    ok &= expect_overlay_text_slot(last_index,
                                   "last near-cap binding after restore");
  }
  ok &= expect_ok("nearCapCode:", &value);
  ok &= expect_int_value(value, 7, "near-cap code after first restore");
  release_value(&value);

  ok &= expect_ok("save:", &value);
  ok &= expect_nil_value(value, "second save: with near-cap overlay");
  release_value(&value);

  if (text_count == 0) {
    ok &= expect_ok("nearCapCode is fn [ 8 ]", &value);
    release_value(&value);
    ok &= expect_ok("restore:", &value);
    ok &= expect_nil_value(value, "second restore: with code-only near-cap overlay");
    release_value(&value);
  } else if (snprintf(mutate_command, sizeof(mutate_command), "t%03zu is \"m\"",
                      last_index) >= (int)sizeof(mutate_command)) {
    fprintf(stderr, "failed to build near-cap mutation command\n");
    ok = 0;
  } else {
    ok &= expect_ok(mutate_command, &value);
    release_value(&value);
    ok &= expect_ok("restore:", &value);
    ok &= expect_nil_value(value, "second restore: with near-cap overlay");
    release_value(&value);
  }

  ok &= expect_live_objects(base_live_objects + overlay_count,
                            "near-cap overlay after second restore");
  if (text_count > 0) {
    ok &= expect_overlay_text_slot(last_index,
                                   "last near-cap binding after second restore");
  }
  ok &= expect_ok("nearCapCode:", &value);
  ok &= expect_int_value(value, 7, "near-cap code after second restore");
  release_value(&value);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_decode_failure_after_reset_re_resets_to_base(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  size_t base_live_objects = 0;
  size_t base_payload = 0;
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }
  base_live_objects = frothy_runtime_live_object_count(runtime());
  base_payload = frothy_runtime_payload_used(runtime());

  ok &= write_simple_text_snapshot();
  ok &= expect_ok("junk is 1", &value);
  release_value(&value);

  frothy_snapshot_test_set_error_after_objects(FROTH_ERROR_HEAP_OUT_OF_MEMORY);
  if (frothy_snapshot_restore() != FROTH_ERROR_HEAP_OUT_OF_MEMORY) {
    fprintf(stderr, "direct restore expected heap out of memory\n");
    ok = 0;
  }
  ok &= expect_live_objects(base_live_objects,
                            "base live objects after decode failure");
  ok &= expect_payload_used(base_payload, "base payload after decode failure");
  ok &= expect_snapshot_present(true, "snapshot preserved after decode failure");
  ok &= expect_error("junk", FROTH_ERROR_UNDEFINED_WORD);
  ok &= expect_ok("1", &value);
  ok &= expect_int_value(value, 1, "prompt usable after decode failure");
  release_value(&value);
  ok &= expect_ok("restore:", &value);
  ok &= expect_nil_value(value, "restore: after decode failure reuse");
  release_value(&value);
  ok &= expect_ok("x", &value);
  ok &= expect_text_value(value, "a", "snapshot restored after decode failure");
  release_value(&value);
  ok &= expect_ok("save:", &value);
  ok &= expect_nil_value(value, "save: after decode failure reuse");
  release_value(&value);

  leave_temp_workspace(&workspace);
  return ok;
}

static froth_error_t test_native_noop(frothy_runtime_t *runtime,
                                      const void *context,
                                      const frothy_value_t *args,
                                      size_t arg_count,
                                      frothy_value_t *out) {
  (void)runtime;
  (void)context;
  (void)args;
  if (arg_count != 0) {
    return FROTH_ERROR_SIGNATURE;
  }
  *out = frothy_value_make_nil();
  return FROTH_OK;
}

static int test_slot_info_errors(void) {
  temp_workspace_t workspace = {{0}};
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_error("slotInfo:", FROTH_ERROR_SIGNATURE);
  ok &= expect_error("slotInfo: \"\"", FROTH_ERROR_BOUNDS);
  ok &= expect_error("slotInfo: @missing", FROTH_ERROR_UNDEFINED_WORD);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_inspect_report_formatting(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  frothy_value_t native_value = frothy_value_make_nil();
  frothy_value_t builtin_alias_value = frothy_value_make_nil();
  char *see_text = NULL;
  char *core_text = NULL;
  char *slot_info_text = NULL;
  char *ffi_info_text = NULL;
  char *base_info_text = NULL;
  char *builtin_see_text = NULL;
  char *native_slot_info_text = NULL;
  char *builtin_alias_info_text = NULL;
  char *cells_info_text = NULL;
  char ffi_expected[256];
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_ok("to inc with x [ x + 1 ]", &value);
  release_value(&value);
  ok &= expect_ok("alias is inc", &value);
  release_value(&value);

  ok &= capture_report_text("alias", FROTHY_INSPECT_REPORT_SEE, &see_text);
  ok &= capture_report_text("alias", FROTHY_INSPECT_REPORT_CORE, &core_text);
  ok &= capture_report_text("alias", FROTHY_INSPECT_REPORT_SLOT_INFO,
                            &slot_info_text);
  ok &= capture_report_text("gpio.mode", FROTHY_INSPECT_REPORT_SLOT_INFO,
                            &ffi_info_text);
  ok &= capture_report_text("A0", FROTHY_INSPECT_REPORT_SLOT_INFO,
                            &base_info_text);
  ok &= capture_report_text("save", FROTHY_INSPECT_REPORT_SEE,
                            &builtin_see_text);
  ok &= expect_ok("frame is cells(1)", &value);
  release_value(&value);
  ok &= capture_report_text("frame", FROTHY_INSPECT_REPORT_SLOT_INFO,
                            &cells_info_text);

  if (ok &&
      frothy_runtime_alloc_native(runtime(), test_native_noop, "test.native", 0,
                                  NULL, &native_value) == FROTH_OK) {
    froth_cell_u_t slot_index = 0;

    ok &= froth_slot_find_name_or_create(&froth_vm.heap, "nativeSlot",
                                         &slot_index) == FROTH_OK;
    ok &= froth_slot_set_overlay(slot_index, 1) == FROTH_OK;
    ok &= froth_slot_set_impl(slot_index, frothy_value_to_cell(native_value)) ==
          FROTH_OK;
    ok &= froth_slot_set_arity(slot_index, 0, 1) == FROTH_OK;
    ok &= capture_report_text("nativeSlot", FROTHY_INSPECT_REPORT_SLOT_INFO,
                              &native_slot_info_text);
  } else if (ok) {
    fprintf(stderr, "failed to allocate native test value\n");
    ok = 0;
  }

  if (ok && frothy_runtime_alloc_native(runtime(), frothy_builtin_save, "save",
                                        0, NULL, &builtin_alias_value) ==
                FROTH_OK) {
    froth_cell_u_t slot_index = 0;

    ok &= froth_slot_find_name_or_create(&froth_vm.heap, "saveAlias",
                                         &slot_index) == FROTH_OK;
    ok &= froth_slot_set_overlay(slot_index, 1) == FROTH_OK;
    ok &= froth_slot_set_impl(slot_index,
                              frothy_value_to_cell(builtin_alias_value)) ==
          FROTH_OK;
    ok &= froth_slot_set_arity(slot_index, 0, 1) == FROTH_OK;
    ok &= capture_report_text("saveAlias", FROTHY_INSPECT_REPORT_SLOT_INFO,
                              &builtin_alias_info_text);
  } else if (ok) {
    fprintf(stderr, "failed to allocate builtin alias test value\n");
    ok = 0;
  }

  if (ok) {
    snprintf(ffi_expected, sizeof(ffi_expected),
             "gpio.mode\n"
             "  slot: base\n"
             "  kind: native\n"
             "  call: 2 -> 1\n"
             "  owner: board ffi\n"
             "  persistence: not saved\n"
             "  effect: ( pin mode -- )\n"
             "  help: Set pin mode (1=output)%s",
             frothy_base_image_has_slot("matrix.init") ? "." : "");
    ok &= expect_text_equal(
        see_text,
        "alias\n"
        "  slot: overlay\n"
        "  kind: code\n"
        "  call: 1 -> 1\n"
        "  owner: overlay image\n"
        "  persistence: saved in snapshot\n"
        "  see: to alias with arg0 [ arg0 + 1 ]",
        "formatted see report");
    ok &= expect_text_equal(
        core_text,
        "alias\n"
        "  slot: overlay\n"
        "  kind: code\n"
        "  call: 1 -> 1\n"
        "  owner: overlay image\n"
        "  persistence: saved in snapshot\n"
        "  core: (fn arity=1 locals=1 (seq (call (builtin \"+\") (read-local 0) (lit 1))))",
        "formatted core report");
    ok &= expect_text_equal(
        slot_info_text,
        "alias\n"
        "  slot: overlay\n"
        "  kind: code\n"
        "  call: 1 -> 1\n"
        "  owner: overlay image\n"
        "  persistence: saved in snapshot",
        "formatted slotInfo report");
    ok &= expect_text_equal(
        ffi_info_text, ffi_expected, "formatted FFI slotInfo report");
    ok &= expect_text_equal(
        base_info_text,
        "A0\n"
        "  slot: base\n"
        "  kind: int\n"
        "  call: not callable\n"
        "  owner: base image\n"
        "  persistence: not saved",
        "formatted base value slotInfo report");
    ok &= expect_text_equal(
        cells_info_text,
        "frame\n"
        "  slot: overlay\n"
        "  kind: cells\n"
        "  call: not callable\n"
        "  owner: overlay image\n"
        "  persistence: saved if contents are persistable",
        "formatted cells slotInfo report");
    ok &= expect_text_equal(
        builtin_see_text,
        "save\n"
        "  slot: base\n"
        "  kind: native\n"
        "  call: 0 -> 1\n"
        "  owner: runtime builtin\n"
        "  persistence: not saved\n"
        "  help: Save the current overlay snapshot.\n"
        "  see: <native save/0>",
        "formatted builtin see report");
    ok &= expect_text_equal(
        native_slot_info_text,
        "nativeSlot\n"
        "  slot: overlay\n"
        "  kind: native\n"
        "  call: 0 -> 1\n"
        "  owner: overlay image\n"
        "  persistence: not saved",
        "formatted custom native slotInfo report");
    ok &= expect_text_equal(
        builtin_alias_info_text,
        "saveAlias\n"
        "  slot: overlay\n"
        "  kind: native\n"
        "  call: 0 -> 1\n"
        "  owner: overlay image\n"
        "  persistence: not saved\n"
        "  help: Save the current overlay snapshot.",
        "formatted builtin alias slotInfo report");
  }

  free(see_text);
  free(core_text);
  free(slot_info_text);
  free(ffi_info_text);
  free(base_info_text);
  free(builtin_see_text);
  free(native_slot_info_text);
  free(builtin_alias_info_text);
  free(cells_info_text);
  if (frothy_snapshot_wipe() != FROTH_OK) {
    fprintf(stderr, "failed to wipe inspect report workspace\n");
    ok = 0;
  }
  leave_temp_workspace(&workspace);
  return ok;
}

static int test_length_aware_slot_lookup(void) {
  temp_workspace_t workspace = {{0}};
  froth_cell_u_t short_slot = 0;
  froth_cell_u_t long_slot = 0;
  froth_cell_u_t found_slot = 0;
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= froth_slot_find_name_or_create_n(&froth_vm.heap, "zz__slot_short", 14,
                                         &short_slot) == FROTH_OK;
  ok &= froth_slot_find_name_or_create_n(&froth_vm.heap,
                                         "zz__slot_short_long", 19,
                                         &long_slot) == FROTH_OK;
  ok &= short_slot != long_slot;
  ok &= froth_slot_find_name_n("zz__slot_short", 14, &found_slot) == FROTH_OK;
  ok &= found_slot == short_slot;
  ok &= froth_slot_find_name_n("zz__slot_short_long", 19, &found_slot) ==
        FROTH_OK;
  ok &= found_slot == long_slot;

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_startup_without_snapshot(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  frothy_startup_report_t report;
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_ok("junk is 1", &value);
  release_value(&value);
  if (frothy_boot_run_startup(&report) != FROTH_OK) {
    fprintf(stderr, "startup without snapshot should not fail\n");
    leave_temp_workspace(&workspace);
    return 0;
  }

  ok &= expect_startup_report(&report, false, FROTH_OK, false, FROTH_OK,
                              "startup without snapshot");
  ok &= expect_error("junk", FROTH_ERROR_UNDEFINED_WORD);
  ok &= expect_ok("1", &value);
  ok &= expect_int_value(value, 1, "prompt usable after no-snapshot startup");
  release_value(&value);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_board_base_library_wipe_restore(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  char *see_before = NULL;
  char *core_before = NULL;
  char *see_after = NULL;
  char *core_after = NULL;
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_binding_view("millis", false, FROTHY_VALUE_CLASS_NATIVE,
                            "base millis view");
  if (frothy_base_image_has_slot("matrix.init")) {
    ok &= expect_binding_view("matrix.init", false, FROTHY_VALUE_CLASS_CODE,
                              "base matrix.init view");
    ok &= capture_code_renders("matrix.init", &see_before, &core_before);
    ok &= expect_ok("matrix.width", &value);
    ok &= expect_int_value(value, 12, "base matrix.width");
    release_value(&value);
    ok &= expect_ok("matrix.init:", &value);
    ok &= expect_nil_value(value, "base matrix.init:");
    release_value(&value);
    ok &= expect_ok("matrix.fill:", &value);
    ok &= expect_nil_value(value, "base matrix.fill:");
    release_value(&value);
    ok &= expect_ok("tm1629.row@: 0", &value);
    ok &= expect_int_value(value, 4095, "base tm1629.row@: 0");
    release_value(&value);

    ok &= expect_ok("to matrix.init [ 99 ]", &value);
    release_value(&value);
    ok &= expect_ok("matrix.width is 99", &value);
    release_value(&value);
    ok &= expect_binding_view("matrix.init", true, FROTHY_VALUE_CLASS_CODE,
                              "overlay matrix.init view");
    ok &= expect_ok("matrix.init:", &value);
    ok &= expect_int_value(value, 99, "overlay matrix.init:");
    release_value(&value);
    ok &= expect_ok("matrix.width", &value);
    ok &= expect_int_value(value, 99, "overlay matrix.width");
    release_value(&value);

    ok &= expect_ok("dangerous.wipe:", &value);
    ok &= expect_nil_value(value, "dangerous.wipe:");
    release_value(&value);
    ok &= expect_ok("tm1629.row@: 0", &value);
    ok &= expect_int_value(value, 0, "tm1629 row resets after wipe");
    release_value(&value);
    ok &= expect_binding_view("matrix.init", false, FROTHY_VALUE_CLASS_CODE,
                              "restored matrix.init view");
    ok &= capture_code_renders("matrix.init", &see_after, &core_after);
    if (ok) {
      ok &= expect_text_equal(see_after, see_before,
                              "matrix.init see after wipe");
      ok &= expect_text_equal(core_after, core_before,
                              "matrix.init core after wipe");
    }
    ok &= expect_ok("matrix.width", &value);
    ok &= expect_int_value(value, 12, "restored matrix.width");
    release_value(&value);
    ok &= expect_ok("matrix.init:", &value);
    ok &= expect_nil_value(value, "restored matrix.init:");
    release_value(&value);
    ok &= expect_ok("matrix.fill:", &value);
    ok &= expect_nil_value(value, "restored matrix.fill:");
    release_value(&value);
    ok &= expect_ok("tm1629.row@: 0", &value);
    ok &= expect_int_value(value, 4095, "restored tm1629.row@: 0");
    release_value(&value);
  } else {
    ok &= expect_binding_view("blink", false, FROTHY_VALUE_CLASS_CODE,
                              "base blink view");
    ok &= expect_binding_view("adc.percent", false, FROTHY_VALUE_CLASS_CODE,
                              "base adc.percent view");
    ok &= capture_code_renders("blink", &see_before, &core_before);
    ok &= expect_ok("adc.percent: A0", &value);
    ok &= expect_int_value(value, 50, "base adc.percent: A0");
    release_value(&value);
    ok &= expect_ok("led.off:", &value);
    ok &= expect_nil_value(value, "led.off:");
    release_value(&value);
    ok &= expect_ok("gpio.read: LED_BUILTIN", &value);
    ok &= expect_int_value(value, 0, "gpio.read after led.off:");
    release_value(&value);
    ok &= expect_ok("led.on:", &value);
    ok &= expect_nil_value(value, "led.on:");
    release_value(&value);
    ok &= expect_ok("gpio.read: LED_BUILTIN", &value);
    ok &= expect_int_value(value, 1, "gpio.read after led.on:");
    release_value(&value);
    ok &= expect_ok("led.toggle:", &value);
    ok &= expect_nil_value(value, "led.toggle:");
    release_value(&value);
    ok &= expect_ok("gpio.read: LED_BUILTIN", &value);
    ok &= expect_int_value(value, 0, "gpio.read after led.toggle:");
    release_value(&value);

    ok &= expect_ok("to blink with pin, count, wait [ 99 ]", &value);
    release_value(&value);
    ok &= expect_ok("to adc.percent with pin [ 99 ]", &value);
    release_value(&value);
    ok &= expect_binding_view("blink", true, FROTHY_VALUE_CLASS_CODE,
                              "overlay blink view");
    ok &= expect_binding_view("adc.percent", true, FROTHY_VALUE_CLASS_CODE,
                              "overlay adc.percent view");
    ok &= expect_ok("adc.percent: A0", &value);
    ok &= expect_int_value(value, 99, "overlay adc.percent: A0");
    release_value(&value);
    ok &= expect_ok("led.on:", &value);
    ok &= expect_nil_value(value, "led.on: before wipe");
    release_value(&value);

    ok &= expect_ok("dangerous.wipe:", &value);
    ok &= expect_nil_value(value, "dangerous.wipe:");
    release_value(&value);
    ok &= expect_ok("gpio.read: LED_BUILTIN", &value);
    ok &= expect_int_value(value, 0, "gpio.read after wipe reset");
    release_value(&value);
    ok &= expect_binding_view("blink", false, FROTHY_VALUE_CLASS_CODE,
                              "restored blink view");
    ok &= expect_binding_view("adc.percent", false, FROTHY_VALUE_CLASS_CODE,
                              "restored adc.percent view");
    ok &= capture_code_renders("blink", &see_after, &core_after);
    if (ok) {
      ok &= expect_text_equal(see_after, see_before, "blink see after wipe");
      ok &= expect_text_equal(core_after, core_before, "blink core after wipe");
    }
    ok &= expect_ok("adc.percent: A0", &value);
    ok &= expect_int_value(value, 50, "restored adc.percent: A0");
    release_value(&value);
  }

  free(see_before);
  free(core_before);
  free(see_after);
  free(core_after);
  leave_temp_workspace(&workspace);
  return ok;
}

static int test_startup_snapshot_discovery_failure(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  frothy_startup_report_t report;
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_ok("junk is 1", &value);
  release_value(&value);

  frothy_boot_test_set_pick_active_error(FROTH_ERROR_IO);
  if (frothy_boot_run_startup(&report) != FROTH_OK) {
    fprintf(stderr,
            "startup snapshot discovery failure should remain recoverable\n");
    leave_temp_workspace(&workspace);
    return 0;
  }

  ok &= expect_startup_report(&report, false, FROTH_ERROR_IO, false, FROTH_OK,
                              "startup snapshot discovery failure");
  ok &= expect_error("junk", FROTH_ERROR_UNDEFINED_WORD);
  ok &= expect_ok("1", &value);
  ok &= expect_int_value(value, 1,
                         "prompt usable after snapshot discovery failure");
  release_value(&value);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_startup_restore_without_boot(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  frothy_startup_report_t report;
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_ok("label is \"ready\"", &value);
  release_value(&value);
  ok &= expect_ok("save:", &value);
  release_value(&value);
  ok &= expect_ok("label is \"mutated\"", &value);
  release_value(&value);
  ok &= prepare_startup_state();

  if (frothy_boot_run_startup(&report) != FROTH_OK) {
    fprintf(stderr, "startup restore without boot should not fail\n");
    leave_temp_workspace(&workspace);
    return 0;
  }

  ok &= expect_startup_report(&report, true, FROTH_OK, false, FROTH_OK,
                              "startup restore without boot");
  ok &= expect_ok("label", &value);
  ok &= expect_text_value(value, "ready", "label after startup restore");
  release_value(&value);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_startup_with_non_code_boot(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  frothy_startup_report_t report;
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_ok("boot is 1", &value);
  release_value(&value);
  ok &= expect_ok("save:", &value);
  release_value(&value);
  ok &= prepare_startup_state();

  if (frothy_boot_run_startup(&report) != FROTH_OK) {
    fprintf(stderr, "startup with non-code boot should not fail\n");
    leave_temp_workspace(&workspace);
    return 0;
  }

  ok &= expect_startup_report(&report, true, FROTH_OK, false, FROTH_OK,
                              "startup with non-code boot");
  ok &= expect_ok("boot", &value);
  ok &= expect_int_value(value, 1, "non-code boot preserved");
  release_value(&value);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_startup_with_successful_boot(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  frothy_startup_report_t report;
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_ok("note is \"saved\"", &value);
  release_value(&value);
  ok &= expect_ok("to boot [ set note to \"booted\" ]", &value);
  release_value(&value);
  ok &= expect_ok("save:", &value);
  release_value(&value);
  ok &= prepare_startup_state();

  if (frothy_boot_run_startup(&report) != FROTH_OK) {
    fprintf(stderr, "startup with successful boot should not fail\n");
    leave_temp_workspace(&workspace);
    return 0;
  }

  ok &= expect_startup_report(&report, true, FROTH_OK, true, FROTH_OK,
                              "startup with successful boot");
  ok &= expect_ok("note", &value);
  ok &= expect_text_value(value, "booted", "successful boot side effect");
  release_value(&value);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_startup_with_failing_boot(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  frothy_startup_report_t report;
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_ok("to boot [ missing ]", &value);
  release_value(&value);
  ok &= expect_ok("save:", &value);
  release_value(&value);
  ok &= prepare_startup_state();

  if (frothy_boot_run_startup(&report) != FROTH_OK) {
    fprintf(stderr, "startup with failing boot should not fail\n");
    leave_temp_workspace(&workspace);
    return 0;
  }

  ok &= expect_startup_report(&report, true, FROTH_OK, true,
                              FROTH_ERROR_UNDEFINED_WORD,
                              "startup with failing boot");
  ok &= expect_ok("1", &value);
  ok &= expect_int_value(value, 1, "prompt usable after failing boot");
  release_value(&value);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_startup_with_bad_arity_boot(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  frothy_startup_report_t report;
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_ok("note is \"saved\"", &value);
  release_value(&value);
  ok &= expect_ok("to boot with value [ set note to \"booted\" ]", &value);
  release_value(&value);
  ok &= expect_ok("save:", &value);
  release_value(&value);
  ok &= prepare_startup_state();

  if (frothy_boot_run_startup(&report) != FROTH_OK) {
    fprintf(stderr, "startup with bad-arity boot should not fail\n");
    leave_temp_workspace(&workspace);
    return 0;
  }

  ok &= expect_startup_report(&report, true, FROTH_OK, true,
                              FROTH_ERROR_SIGNATURE,
                              "startup with bad-arity boot");
  ok &= expect_ok("note", &value);
  ok &= expect_text_value(value, "saved",
                          "saved overlay preserved after bad-arity boot");
  release_value(&value);
  ok &= expect_ok("3", &value);
  ok &= expect_int_value(value, 3, "prompt usable after bad-arity boot");
  release_value(&value);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_startup_with_interrupted_boot(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  frothy_startup_report_t report;
  pid_t child = -1;
  int status = 0;
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }
  if (!ensure_platform_runtime()) {
    leave_temp_workspace(&workspace);
    return 0;
  }

  ok &= expect_ok("to boot [ while true [ 1 ] ]", &value);
  release_value(&value);
  ok &= expect_ok("save:", &value);
  release_value(&value);
  ok &= prepare_startup_state();
  child = fork();
  if (child < 0) {
    perror("fork");
    leave_temp_workspace(&workspace);
    return 0;
  }
  if (child == 0) {
    usleep(10000);
    kill(getppid(), SIGINT);
    _exit(0);
  }

  if (frothy_boot_run_startup(&report) != FROTH_OK) {
    fprintf(stderr, "startup with interrupted boot should not fail\n");
    (void)waitpid(child, &status, 0);
    leave_temp_workspace(&workspace);
    return 0;
  }
  if (waitpid(child, &status, 0) < 0) {
    perror("waitpid");
    leave_temp_workspace(&workspace);
    return 0;
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fprintf(stderr, "interrupt helper process failed\n");
    leave_temp_workspace(&workspace);
    return 0;
  }

  ok &= expect_startup_report(&report, true, FROTH_OK, true,
                              FROTH_ERROR_PROGRAM_INTERRUPTED,
                              "startup with interrupted boot");
  ok &= expect_ok("2", &value);
  ok &= expect_int_value(value, 2, "prompt usable after interrupted boot");
  release_value(&value);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_wipe_inside_nested_call_unwinds_cleanly(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_ok("runner is fn with v [ dangerous.wipe: ]", &value);
  release_value(&value);
  ok &= expect_ok("outer is fn with v [ runner: v; 99 ]", &value);
  release_value(&value);
  ok &= expect_ok("outer: \"temp\"", &value);
  ok &= expect_nil_value(value, "outer: temp");
  release_value(&value);

  ok &= expect_error("outer", FROTH_ERROR_UNDEFINED_WORD);
  ok &= expect_error("runner", FROTH_ERROR_UNDEFINED_WORD);
  ok &= expect_ok("2", &value);
  ok &= expect_int_value(value, 2, "prompt usable after nested wipe");
  release_value(&value);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_record_roundtrip_and_truthful_inspection(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_ok("record Point [ x, y ]", &value);
  release_value(&value);
  ok &= expect_ok("point is Point: 10, 20", &value);
  release_value(&value);
  ok &= expect_ok("frame is cells(1)", &value);
  release_value(&value);
  ok &= expect_ok("set frame[0] to point", &value);
  release_value(&value);
  ok &= expect_binding_render_view("Point", FROTHY_VALUE_CLASS_RECORD_DEF,
                            "record-def",
                            "record Point [ x, y ]", "record def before save");
  ok &= expect_binding_render_view("point", FROTHY_VALUE_CLASS_RECORD, "record",
                            "Point: 10, 20", "record before save");

  ok &= expect_ok("save:", &value);
  ok &= expect_nil_value(value, "save: record roundtrip");
  release_value(&value);

  ok &= expect_ok("record Point [ z ]", &value);
  release_value(&value);
  ok &= expect_ok("point is Point: 99", &value);
  release_value(&value);
  ok &= expect_ok("restore:", &value);
  ok &= expect_nil_value(value, "restore: record roundtrip");
  release_value(&value);

  ok &= expect_binding_render_view("Point", FROTHY_VALUE_CLASS_RECORD_DEF,
                            "record-def",
                            "record Point [ x, y ]",
                            "record def after restore");
  ok &= expect_binding_render_view("point", FROTHY_VALUE_CLASS_RECORD, "record",
                            "Point: 10, 20", "record after restore");
  ok &= expect_ok("point->x", &value);
  ok &= expect_int_value(value, 10, "point->x after restore");
  release_value(&value);
  ok &= expect_ok("frame[0]->y", &value);
  ok &= expect_int_value(value, 20, "frame[0]->y after restore");
  release_value(&value);
  ok &= expect_ok("restoredPoint is Point: 1, 2", &value);
  release_value(&value);
  ok &= expect_binding_render_view("restoredPoint", FROTHY_VALUE_CLASS_RECORD, "record",
                            "Point: 1, 2",
                            "restored constructor after restore");
  ok &= expect_error("Point: 1", FROTH_ERROR_SIGNATURE);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_prefixed_record_roundtrip(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_ok(
      "in sprite [ record State [ x, y ]; current is State: 3, 4 ]", &value);
  release_value(&value);
  ok &= expect_binding_render_view("sprite.State", FROTHY_VALUE_CLASS_RECORD_DEF,
                                   "record-def",
                                   "record sprite.State [ x, y ]",
                                   "prefixed record def before save");
  ok &= expect_ok("save:", &value);
  ok &= expect_nil_value(value, "save: prefixed record roundtrip");
  release_value(&value);

  ok &= expect_ok(
      "in sprite [ record State [ z ]; current is State: 9 ]", &value);
  release_value(&value);
  ok &= expect_ok("restore:", &value);
  ok &= expect_nil_value(value, "restore: prefixed record roundtrip");
  release_value(&value);

  ok &= expect_binding_render_view("sprite.State", FROTHY_VALUE_CLASS_RECORD_DEF,
                                   "record-def",
                                   "record sprite.State [ x, y ]",
                                   "prefixed record def after restore");
  ok &= expect_ok("sprite.current->x", &value);
  ok &= expect_int_value(value, 3, "sprite.current->x after restore");
  release_value(&value);
  ok &= expect_ok("sprite.current->y", &value);
  ok &= expect_int_value(value, 4, "sprite.current->y after restore");
  release_value(&value);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_punctuated_names_roundtrip(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_ok(
      "in demo [ brightness! is 3; record Glyph! [ x?, y@ ]; current is Glyph!: 7, 9 ]",
      &value);
  release_value(&value);
  ok &= expect_ok("save:", &value);
  ok &= expect_nil_value(value, "save: punctuated names roundtrip");
  release_value(&value);

  ok &= expect_ok(
      "in demo [ brightness! is 99; record Glyph! [ z ]; current is Glyph!: 1 ]",
      &value);
  release_value(&value);
  ok &= expect_ok("restore:", &value);
  ok &= expect_nil_value(value, "restore: punctuated names roundtrip");
  release_value(&value);

  ok &= expect_ok("demo.brightness!", &value);
  ok &= expect_int_value(value, 3, "demo.brightness! after restore");
  release_value(&value);
  ok &= expect_binding_render_view("demo.Glyph!", FROTHY_VALUE_CLASS_RECORD_DEF,
                                   "record-def",
                                   "record demo.Glyph! [ x?, y@ ]",
                                   "punctuated record def after restore");
  ok &= expect_ok("demo.current->x?", &value);
  ok &= expect_int_value(value, 7, "demo.current->x? after restore");
  release_value(&value);
  ok &= expect_ok("demo.current->y@", &value);
  ok &= expect_int_value(value, 9, "demo.current->y@ after restore");
  release_value(&value);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_record_cycle_save_rejection(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= expect_ok("record Node [ next ]", &value);
  release_value(&value);
  ok &= expect_ok("node is Node: nil", &value);
  release_value(&value);
  ok &= expect_ok("set node->next to node", &value);
  release_value(&value);
  ok &= expect_binding_render_view("node", FROTHY_VALUE_CLASS_RECORD, "record",
                            "Node: <cycle>", "cyclic record render");
  ok &= expect_error("save:", FROTH_ERROR_NOT_PERSISTABLE);
  ok &= expect_snapshot_present(false, "no snapshot after cyclic record save");
  ok &= expect_ok("node->next", &value);
  release_value(&value);
  ok &= expect_ok("1", &value);
  ok &= expect_int_value(value, 1, "prompt usable after cyclic save failure");
  release_value(&value);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_record_snapshot_rejects_mismatched_record_def_name(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  record_snapshot_offsets_t offsets;
  uint8_t bad_name_byte = 'Q';
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= write_simple_record_snapshot();
  ok &= locate_record_snapshot_offsets(&offsets);
  ok &= patch_active_snapshot_payload(offsets.record_def_name_offset,
                                      &bad_name_byte, sizeof(bad_name_byte),
                                      true);
  ok &= expect_ok("junk is 1", &value);
  release_value(&value);
  ok &= expect_error("restore:", FROTH_ERROR_SNAPSHOT_BAD_NAME);
  ok &= expect_error("junk", FROTH_ERROR_UNDEFINED_WORD);
  ok &= expect_error("Point", FROTH_ERROR_UNDEFINED_WORD);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_record_snapshot_rejects_record_arity_mismatch(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t value = frothy_value_make_nil();
  record_snapshot_offsets_t offsets;
  uint32_t bad_field_count = 1;
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  ok &= write_simple_record_snapshot();
  ok &= locate_record_snapshot_offsets(&offsets);
  ok &= patch_active_snapshot_payload(offsets.record_field_count_offset,
                                      &bad_field_count,
                                      sizeof(bad_field_count), true);
  ok &= expect_ok("junk is 1", &value);
  release_value(&value);
  ok &= expect_error("restore:", FROTH_ERROR_SNAPSHOT_FORMAT);
  ok &= expect_error("junk", FROTH_ERROR_UNDEFINED_WORD);
  ok &= expect_error("Point", FROTH_ERROR_UNDEFINED_WORD);

  leave_temp_workspace(&workspace);
  return ok;
}

static int test_non_persistable_rejection(void) {
  temp_workspace_t workspace = {{0}};
  frothy_value_t native_value = frothy_value_make_nil();
  frothy_value_t result = frothy_value_make_nil();
  froth_cell_u_t slot_index = 0;
  int ok = 1;

  if (!enter_temp_workspace(&workspace)) {
    return 0;
  }

  if (frothy_runtime_alloc_native(runtime(), test_native_noop, "test.native", 0,
                                  NULL, &native_value) != FROTH_OK) {
    fprintf(stderr, "failed to allocate native test value\n");
    leave_temp_workspace(&workspace);
    return 0;
  }
  if (froth_slot_find_name_or_create(&froth_vm.heap, "nativeSlot", &slot_index) !=
          FROTH_OK ||
      froth_slot_set_overlay(slot_index, 1) != FROTH_OK ||
      froth_slot_set_impl(slot_index, frothy_value_to_cell(native_value)) !=
          FROTH_OK ||
      froth_slot_set_arity(slot_index, 0, 1) != FROTH_OK) {
    fprintf(stderr, "failed to bind native test slot\n");
    leave_temp_workspace(&workspace);
    return 0;
  }

  ok &= expect_error("save:", FROTH_ERROR_NOT_PERSISTABLE);
  ok &= expect_snapshot_present(false, "no snapshot after non-persistable save");
  ok &= expect_ok("dangerous.wipe:", &result);
  ok &= expect_nil_value(result, "dangerous.wipe after non-persistable save");
  release_value(&result);

  leave_temp_workspace(&workspace);
  return ok;
}

int main(void) {
  int ok = 1;

  ok &= test_native_dispatch_and_roundtrip();
  ok &= test_readability_snapshot_roundtrip();
  ok &= test_overlay_reset_semantics();
  ok &= test_restore_without_snapshot_resets_to_base();
  ok &= test_corrupt_snapshot_failures_reset_to_base();
  ok &= test_repeated_near_capacity_save_restore();
  ok &= test_decode_failure_after_reset_re_resets_to_base();
  ok &= test_wipe_inside_nested_call_unwinds_cleanly();
  ok &= test_record_roundtrip_and_truthful_inspection();
  ok &= test_prefixed_record_roundtrip();
  ok &= test_punctuated_names_roundtrip();
  ok &= test_record_cycle_save_rejection();
  ok &= test_record_snapshot_rejects_mismatched_record_def_name();
  ok &= test_record_snapshot_rejects_record_arity_mismatch();
  ok &= test_non_persistable_rejection();
  ok &= test_slot_info_errors();
  ok &= test_inspect_report_formatting();
  ok &= test_length_aware_slot_lookup();
  ok &= test_startup_without_snapshot();
  ok &= test_board_base_library_wipe_restore();
  ok &= test_startup_snapshot_discovery_failure();
  ok &= test_startup_restore_without_boot();
  ok &= test_startup_with_non_code_boot();
  ok &= test_startup_with_successful_boot();
  ok &= test_startup_with_failing_boot();
  ok &= test_startup_with_bad_arity_boot();
  ok &= test_startup_with_interrupted_boot();

  return ok ? 0 : 1;
}
