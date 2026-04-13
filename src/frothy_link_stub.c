#include "froth_link.h"

froth_error_t froth_link_send_hello_res(froth_vm_t *vm, uint64_t session_id,
                                        uint16_t seq) {
  (void)vm;
  (void)session_id;
  (void)seq;
  return FROTH_ERROR_LINK_UNKNOWN_TYPE;
}

froth_error_t froth_link_dispatch(froth_vm_t *vm,
                                  const froth_link_header_t *header,
                                  const uint8_t *payload) {
  (void)vm;
  (void)header;
  (void)payload;
  return FROTH_ERROR_LINK_UNKNOWN_TYPE;
}
