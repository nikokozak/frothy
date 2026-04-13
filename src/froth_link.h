#pragma once

#include "froth_transport.h"
#include "froth_types.h"

froth_error_t froth_link_send_hello_res(froth_vm_t *vm, uint64_t session_id,
                                        uint16_t seq);

froth_error_t froth_link_dispatch(froth_vm_t *vm,
                                  const froth_link_header_t *header,
                                  const uint8_t *payload);
