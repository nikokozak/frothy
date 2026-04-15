#pragma once

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline bool frothy_name_byte_is_start(unsigned char byte) {
  return isalpha((int)byte) || byte == '_';
}

static inline bool frothy_name_byte_is_simple_continue(unsigned char byte) {
  return isalnum((int)byte) || byte == '_' || byte == '!' || byte == '?' ||
         byte == '@';
}

static inline bool frothy_name_byte_is_slot_continue(unsigned char byte) {
  return frothy_name_byte_is_simple_continue(byte) || byte == '.';
}

static inline bool frothy_simple_name_bytes_are_valid(const uint8_t *bytes,
                                                      size_t length) {
  size_t i;

  if (bytes == NULL || length == 0 ||
      !frothy_name_byte_is_start((unsigned char)bytes[0])) {
    return false;
  }
  for (i = 1; i < length; i++) {
    if (bytes[i] == '\0' ||
        !frothy_name_byte_is_simple_continue((unsigned char)bytes[i])) {
      return false;
    }
  }
  return true;
}

static inline bool frothy_slot_name_bytes_are_valid(const uint8_t *bytes,
                                                    size_t length) {
  size_t i;
  bool expect_segment_start = true;

  if (bytes == NULL || length == 0) {
    return false;
  }
  for (i = 0; i < length; i++) {
    if (bytes[i] == '\0') {
      return false;
    }
    if (expect_segment_start) {
      if (!frothy_name_byte_is_start((unsigned char)bytes[i])) {
        return false;
      }
      expect_segment_start = false;
      continue;
    }
    if (bytes[i] == '.') {
      expect_segment_start = true;
      continue;
    }
    if (!frothy_name_byte_is_simple_continue((unsigned char)bytes[i])) {
      return false;
    }
  }
  return !expect_segment_start;
}
