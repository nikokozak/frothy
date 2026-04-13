#include "platform.h"
#include "froth_types.h"
#include "froth_vm.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

// Keep term state so we can reset at end.
static struct termios original;
static struct termios modified;
static int term_configured = 0;
static platform_emit_hook_t emit_hook = NULL;
static void *emit_hook_context = NULL;

static void interrupt_handler(int signum) {
  if (signum != SIGINT) {
    return;
  }
  froth_vm.interrupted = 1;
  return;
}

void platform_delay_ms(froth_cell_u_t ms) { usleep((useconds_t)ms * 1000); }

uint32_t platform_uptime_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void platform_reset_runtime_state(void) {}

static void cleanup_term(void) {
  if (!term_configured)
    return;
  tcsetattr(STDIN_FILENO, TCSANOW, &original);
}

static froth_error_t platform_write_byte(int fd, uint8_t byte) {
  while (1) {
    ssize_t written = write(fd, &byte, 1);

    if (written == 1) {
      return FROTH_OK;
    }
    if (written < 0 && errno == EINTR) {
      continue;
    }
    return FROTH_ERROR_IO;
  }
}

froth_error_t platform_init(void) {
  static struct sigaction sig;
  sigemptyset(&sig.sa_mask);
  sig.sa_flags = 0;
  sig.sa_handler = interrupt_handler;

  if (sigaction(SIGINT, &sig, NULL) == -1) {
    return FROTH_ERROR_IO;
  }

  setvbuf(stdin, NULL, _IONBF, 0);
  setvbuf(stdout, NULL, _IONBF, 0);

  // We need to set term behavior so that it matches
  // a "raw" state that most embedded devices have, and so
  // we can build on it for things like multiline, etc.
  if (isatty(STDIN_FILENO)) {
    if (tcgetattr(STDIN_FILENO, &original) == -1) {
      return FROTH_ERROR_IO;
    }
    modified = original;
    modified.c_lflag &= ~ECHO;
    modified.c_lflag &= ~ICANON;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &modified) == -1) {
      return FROTH_ERROR_IO;
    }
    term_configured = 1;
  }

  atexit(cleanup_term);

  return FROTH_OK;
}

froth_error_t platform_emit(uint8_t byte) {
  if (emit_hook != NULL) {
    return emit_hook(emit_hook_context, byte);
  }
  return platform_write_byte(STDOUT_FILENO, byte);
}

froth_error_t platform_emit_raw(uint8_t byte) {
  return platform_write_byte(STDOUT_FILENO, byte);
}

void platform_set_emit_hook(platform_emit_hook_t hook, void *context) {
  emit_hook = hook;
  emit_hook_context = context;
}

void platform_clear_emit_hook(void) {
  emit_hook = NULL;
  emit_hook_context = NULL;
}

froth_error_t platform_key(uint8_t *byte) {
  int c = fgetc(stdin);
  if (c == EOF) {
    return FROTH_ERROR_IO;
  }
  *byte = (uint8_t)c;
  return FROTH_OK;
}

bool platform_input_closed(void) { return feof(stdin) != 0; }

bool platform_should_echo_input(void) { return isatty(STDIN_FILENO) != 0; }

bool platform_key_ready(void) {
  struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN};
  return poll(&pfd, 1, 0) > 0;
}

void platform_check_interrupt(struct froth_vm_t *vm) {
  (void)vm; // SIGINT handler sets vm->interrupted asynchronously
}

void platform_fatal(void) { exit(1); }

#ifdef FROTH_HAS_SNAPSHOTS
static const char *snap_path(uint8_t slot) {
  return slot == 0 ? FROTH_SNAPSHOT_PATH_A : FROTH_SNAPSHOT_PATH_B;
}

froth_error_t platform_snapshot_read(uint8_t slot, uint32_t offset,
                                     uint8_t *buf, uint32_t len) {
  const char *file = snap_path(slot);
  FILE *file_ptr;
  file_ptr = fopen(file, "rb");

  if (file_ptr == NULL) {
    return FROTH_ERROR_IO;
  }

  if (fseek(file_ptr, offset, SEEK_SET)) {
    fclose(file_ptr);
    return FROTH_ERROR_IO;
  }
  if (!fread(buf, len, 1, file_ptr)) {
    fclose(file_ptr);
    return FROTH_ERROR_IO;
  }

  fclose(file_ptr);
  return FROTH_OK;
}

froth_error_t platform_snapshot_write(uint8_t slot, uint32_t offset,
                                      const uint8_t *buf, uint32_t len) {
  const char *file = snap_path(slot);
  FILE *file_ptr;
  file_ptr = fopen(file, "r+b");

  if (file_ptr == NULL) {
    file_ptr = fopen(file, "w+b");
    if (file_ptr == NULL) {
      return FROTH_ERROR_IO;
    }
  }

  if (fseek(file_ptr, offset, SEEK_SET)) {
    fclose(file_ptr);
    return FROTH_ERROR_IO;
  }
  if (!fwrite(buf, len, 1, file_ptr)) {
    fclose(file_ptr);
    return FROTH_ERROR_IO;
  }

  fclose(file_ptr);
  return FROTH_OK;
}

froth_error_t platform_snapshot_erase(uint8_t slot) {
  const char *file = snap_path(slot);
  if (remove(file) && errno != ENOENT) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}
#endif
