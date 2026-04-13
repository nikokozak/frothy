#include "ffi.h"
#include "froth_fmt.h"
#include <stdint.h>
#include <unistd.h>

/* POSIX board package: stub GPIO + real ms delay.
 * gpio.mode and gpio.write print trace output so you can
 * "see" a blink demo in the terminal. */

#define POSIX_I2C_MAX_BUSES 2
#define POSIX_I2C_MAX_DEVICES 8
#define POSIX_UART_MAX_PORTS 2

typedef struct {
  int in_use;
  froth_cell_t sda;
  froth_cell_t scl;
  froth_cell_t freq;
} posix_i2c_bus_t;

typedef struct {
  int in_use;
  froth_cell_t bus;
  froth_cell_t addr;
  froth_cell_t speed;
} posix_i2c_device_t;

typedef struct {
  int in_use;
  froth_cell_t tx;
  froth_cell_t rx;
  froth_cell_t baud;
  uint8_t read_index;
} posix_uart_t;

static posix_i2c_bus_t posix_i2c_buses[POSIX_I2C_MAX_BUSES];
static posix_i2c_device_t posix_i2c_devices[POSIX_I2C_MAX_DEVICES];
static posix_uart_t posix_uarts[POSIX_UART_MAX_PORTS];
static const uint8_t posix_uart_readback[] = {'f', 'r', 'o', 't', 'h'};

static froth_error_t emit_trace_prefix(const char *prefix, froth_cell_t handle) {
  FROTH_TRY(emit_string(prefix));
  FROTH_TRY(emit_string(format_number(handle)));
  FROTH_TRY(emit_string("] "));
  return FROTH_OK;
}

FROTH_FFI_ARITY(prim_gpio_mode, "gpio.mode", "( pin mode -- )", 2, 0,
                "Set pin mode (1=output)") {
  FROTH_POP(mode);
  FROTH_POP(pin);
  emit_string("[gpio] pin ");
  emit_string(format_number(pin));
  emit_string(mode == 1 ? " -> OUTPUT\n" : " -> INPUT\n");
  return FROTH_OK;
}

FROTH_FFI_ARITY(prim_gpio_write, "gpio.write", "( pin value -- )", 2, 0,
                "Write digital output") {
  FROTH_POP(value);
  FROTH_POP(pin);
  emit_string("[gpio] pin ");
  emit_string(format_number(pin));
  emit_string(value ? " = HIGH\n" : " = LOW\n");
  return FROTH_OK;
}

FROTH_FFI_ARITY(prim_ms, "ms", "( n -- )", 1, 0, "Delay n milliseconds") {
  FROTH_POP(ms);
  usleep((useconds_t)ms * 1000);
  return FROTH_OK;
}

FROTH_FFI_ARITY(prim_adc_read, "adc.read", "( pin -- value )", 1, 1,
                "Deterministic ADC stub on POSIX") {
  FROTH_POP(pin);

  if (pin < 0) {
    return FROTH_ERROR_BOUNDS;
  }

  FROTH_PUSH(2048 + (pin & 0xff));
  return FROTH_OK;
}

FROTH_FFI_ARITY(prim_i2c_init, "i2c.init", "( sda scl freq -- bus )", 3, 1,
                "Stub I2C bus init on POSIX") {
  FROTH_POP(freq);
  FROTH_POP(scl);
  FROTH_POP(sda);

  for (int i = 0; i < POSIX_I2C_MAX_BUSES; i++) {
    if (posix_i2c_buses[i].in_use) {
      continue;
    }
    posix_i2c_buses[i] =
        (posix_i2c_bus_t){.in_use = 1, .sda = sda, .scl = scl, .freq = freq};
    FROTH_PUSH(i);
    return FROTH_OK;
  }

  return FROTH_ERROR_BOUNDS;
}

FROTH_FFI_ARITY(prim_i2c_add_device, "i2c.add-device", "( bus addr speed -- device )",
                3, 1, "Stub I2C device add on POSIX") {
  FROTH_POP(speed);
  FROTH_POP(addr);
  FROTH_POP(bus);

  if (bus < 0 || bus >= POSIX_I2C_MAX_BUSES || !posix_i2c_buses[bus].in_use) {
    return FROTH_ERROR_BOUNDS;
  }

  for (int i = 0; i < POSIX_I2C_MAX_DEVICES; i++) {
    if (posix_i2c_devices[i].in_use) {
      continue;
    }
    posix_i2c_devices[i] = (posix_i2c_device_t){
        .in_use = 1, .bus = bus, .addr = addr, .speed = speed};
    FROTH_PUSH(i);
    return FROTH_OK;
  }

  return FROTH_ERROR_BOUNDS;
}

FROTH_FFI_ARITY(prim_i2c_rm_device, "i2c.rm-device", "( device -- )", 1, 0,
                "Stub I2C device remove on POSIX") {
  FROTH_POP(device);
  if (device < 0 || device >= POSIX_I2C_MAX_DEVICES ||
      !posix_i2c_devices[device].in_use) {
    return FROTH_ERROR_BOUNDS;
  }
  posix_i2c_devices[device] = (posix_i2c_device_t){0};
  return FROTH_OK;
}

FROTH_FFI_ARITY(prim_i2c_del_bus, "i2c.del-bus", "( bus -- )", 1, 0,
                "Stub I2C bus delete on POSIX") {
  FROTH_POP(bus);
  if (bus < 0 || bus >= POSIX_I2C_MAX_BUSES || !posix_i2c_buses[bus].in_use) {
    return FROTH_ERROR_BOUNDS;
  }
  posix_i2c_buses[bus] = (posix_i2c_bus_t){0};
  return FROTH_OK;
}

FROTH_FFI_ARITY(prim_i2c_probe, "i2c.probe", "( bus addr -- flag )", 2, 1,
                "Stub I2C probe on POSIX") {
  FROTH_POP(addr);
  FROTH_POP(bus);
  if (bus < 0 || bus >= POSIX_I2C_MAX_BUSES || !posix_i2c_buses[bus].in_use) {
    return FROTH_ERROR_BOUNDS;
  }
  FROTH_PUSH((addr >= 0 && addr <= 0x7f) ? -1 : 0);
  return FROTH_OK;
}

FROTH_FFI_ARITY(prim_i2c_write_byte, "i2c.write-byte", "( device byte -- )", 2, 0,
                "Stub I2C write byte on POSIX") {
  FROTH_POP(byte);
  FROTH_POP(device);
  if (device < 0 || device >= POSIX_I2C_MAX_DEVICES ||
      !posix_i2c_devices[device].in_use) {
    return FROTH_ERROR_BOUNDS;
  }
  emit_trace_prefix("[i2c", device);
  emit_string("write-byte ");
  emit_string(format_number(byte));
  emit_string("\n");
  return FROTH_OK;
}

FROTH_FFI_ARITY(prim_i2c_read_byte, "i2c.read-byte", "( device -- byte )", 1, 1,
                "Stub I2C read byte on POSIX") {
  FROTH_POP(device);
  if (device < 0 || device >= POSIX_I2C_MAX_DEVICES ||
      !posix_i2c_devices[device].in_use) {
    return FROTH_ERROR_BOUNDS;
  }
  FROTH_PUSH(posix_i2c_devices[device].addr & 0xff);
  return FROTH_OK;
}

FROTH_FFI_ARITY(prim_i2c_write_reg, "i2c.write-reg", "( byte device reg -- )", 3,
                0, "Stub I2C write register on POSIX") {
  FROTH_POP(reg);
  FROTH_POP(device);
  FROTH_POP(byte);
  if (device < 0 || device >= POSIX_I2C_MAX_DEVICES ||
      !posix_i2c_devices[device].in_use) {
    return FROTH_ERROR_BOUNDS;
  }
  emit_trace_prefix("[i2c", device);
  emit_string("write-reg ");
  emit_string(format_number(reg));
  emit_string(" <- ");
  emit_string(format_number(byte));
  emit_string("\n");
  return FROTH_OK;
}

FROTH_FFI_ARITY(prim_i2c_read_reg, "i2c.read-reg", "( device reg -- byte )", 2, 1,
                "Stub I2C read register on POSIX") {
  FROTH_POP(reg);
  FROTH_POP(device);
  if (device < 0 || device >= POSIX_I2C_MAX_DEVICES ||
      !posix_i2c_devices[device].in_use) {
    return FROTH_ERROR_BOUNDS;
  }
  FROTH_PUSH((posix_i2c_devices[device].addr + reg) & 0xff);
  return FROTH_OK;
}

FROTH_FFI_ARITY(prim_i2c_read_reg16, "i2c.read-reg16", "( device reg -- word )", 2,
                1, "Stub I2C read 16-bit register on POSIX") {
  FROTH_POP(reg);
  FROTH_POP(device);
  if (device < 0 || device >= POSIX_I2C_MAX_DEVICES ||
      !posix_i2c_devices[device].in_use) {
    return FROTH_ERROR_BOUNDS;
  }
  FROTH_PUSH(((posix_i2c_devices[device].addr & 0xff) << 8) | (reg & 0xff));
  return FROTH_OK;
}

FROTH_FFI_ARITY(prim_uart_init, "uart.init", "( tx rx baud -- uart )", 3, 1,
                "Stub UART init on POSIX") {
  FROTH_POP(baud);
  FROTH_POP(rx);
  FROTH_POP(tx);

  for (int i = 0; i < POSIX_UART_MAX_PORTS; i++) {
    if (posix_uarts[i].in_use) {
      continue;
    }
    posix_uarts[i] = (posix_uart_t){
        .in_use = 1, .tx = tx, .rx = rx, .baud = baud, .read_index = 0};
    FROTH_PUSH(i);
    return FROTH_OK;
  }

  return FROTH_ERROR_BOUNDS;
}

FROTH_FFI_ARITY(prim_uart_write, "uart.write", "( byte uart -- )", 2, 0,
                "Stub UART write byte on POSIX") {
  FROTH_POP(uart);
  FROTH_POP(byte);

  if (uart < 0 || uart >= POSIX_UART_MAX_PORTS || !posix_uarts[uart].in_use) {
    return FROTH_ERROR_BOUNDS;
  }

  return platform_emit((uint8_t)(byte & 0xff));
}

FROTH_FFI_ARITY(prim_uart_read, "uart.read", "( uart -- byte )", 1, 1,
                "Stub UART read byte on POSIX") {
  FROTH_POP(uart);

  if (uart < 0 || uart >= POSIX_UART_MAX_PORTS || !posix_uarts[uart].in_use) {
    return FROTH_ERROR_BOUNDS;
  }

  uint8_t byte =
      posix_uart_readback[posix_uarts[uart].read_index %
                          (uint8_t)sizeof(posix_uart_readback)];
  posix_uarts[uart].read_index++;
  FROTH_PUSH(byte);
  return FROTH_OK;
}

FROTH_FFI_ARITY(prim_uart_available, "uart.key?", "( uart -- flag )", 1, 1,
                "Stub UART key? on POSIX (always true)") {
  FROTH_POP(uart);

  if (uart < 0 || uart >= POSIX_UART_MAX_PORTS || !posix_uarts[uart].in_use) {
    return FROTH_ERROR_BOUNDS;
  }

  FROTH_PUSH(-1);
  return FROTH_OK;
}

FROTH_BOARD_BEGIN(froth_board_bindings)
FROTH_BIND(prim_gpio_mode), FROTH_BIND(prim_gpio_write),
    FROTH_BIND(prim_ms), FROTH_BIND(prim_adc_read),
    FROTH_BIND(prim_i2c_init),
    FROTH_BIND(prim_i2c_add_device), FROTH_BIND(prim_i2c_rm_device),
    FROTH_BIND(prim_i2c_del_bus), FROTH_BIND(prim_i2c_probe),
    FROTH_BIND(prim_i2c_write_byte), FROTH_BIND(prim_i2c_read_byte),
    FROTH_BIND(prim_i2c_write_reg), FROTH_BIND(prim_i2c_read_reg),
    FROTH_BIND(prim_i2c_read_reg16), FROTH_BIND(prim_uart_init),
    FROTH_BIND(prim_uart_write), FROTH_BIND(prim_uart_read),
    FROTH_BIND(prim_uart_available), FROTH_BOARD_END
