/* TODO: ESP32 DevKit V1 board FFI bindings */

#include "ffi.h"
#include "driver/adc.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "froth_console.h"
#include "froth_fmt.h"
#include "froth_types.h"
#include "platform.h"

static froth_error_t throw_program_interrupted(froth_vm_t *froth_vm) {
  froth_vm->interrupted = 0;
  froth_vm->thrown = FROTH_ERROR_PROGRAM_INTERRUPTED;
  return FROTH_ERROR_THROW;
}

static froth_error_t poll_interruptible_wait(froth_vm_t *froth_vm) {
  froth_console_poll(froth_vm);
  if (froth_vm->interrupted) {
    return throw_program_interrupted(froth_vm);
  }
  return FROTH_OK;
}

#if defined(ADC_ATTEN_DB_12)
#define FROTH_BOARD_ADC_ATTEN ADC_ATTEN_DB_12
#else
#define FROTH_BOARD_ADC_ATTEN ADC_ATTEN_DB_11
#endif

static bool esp32_adc1_channel_for_pin(froth_cell_t pin,
                                       adc1_channel_t *channel_out) {
  switch (pin) {
  case 32:
    *channel_out = ADC1_CHANNEL_4;
    return true;
  case 33:
    *channel_out = ADC1_CHANNEL_5;
    return true;
  case 34:
    *channel_out = ADC1_CHANNEL_6;
    return true;
  case 35:
    *channel_out = ADC1_CHANNEL_7;
    return true;
  case 36:
    *channel_out = ADC1_CHANNEL_0;
    return true;
  case 37:
    *channel_out = ADC1_CHANNEL_1;
    return true;
  case 38:
    *channel_out = ADC1_CHANNEL_2;
    return true;
  case 39:
    *channel_out = ADC1_CHANNEL_3;
    return true;
  default:
    return false;
  }
}

FROTH_FFI_ARITY(esp32_gpio_mode, "gpio.mode", "( pin mode -- )", 2, 0,
                "Set pin mode (1=output)") {
  FROTH_POP(mode);
  FROTH_POP(pin);

  esp_err_t err =
      gpio_set_direction(pin, mode == 1 ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_gpio_write, "gpio.write", "( pin level -- )", 2, 0,
                "Set pin level (1=high)") {
  FROTH_POP(level);
  FROTH_POP(pin);

  esp_err_t err = gpio_set_level(pin, level);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI_ARITY(
    esp32_gpio_read, "gpio.read", "( pin -- level )", 1, 1,
    "Read pin level. Pin mode MUST be set, otherwise will always return 0.") {
  FROTH_POP(pin);

  froth_cell_t level = gpio_get_level(pin);
  FROTH_PUSH(level);
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_ms, "ms", "( ms -- )", 1, 0,
                "Sleep for a given amount of ms.") {
  FROTH_POP(ms);

  if (ms <= 0) {
    return FROTH_OK;
  }

  while (ms > 0) {
    froth_cell_t chunk = ms > 10 ? 10 : ms;
    vTaskDelay(pdMS_TO_TICKS(chunk));
    ms -= chunk;
    FROTH_TRY(poll_interruptible_wait(froth_vm));
  }

  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_adc_read, "adc.read", "( pin -- value )", 1, 1,
                "Read a 12-bit ADC1 sample from a GPIO pin.") {
  adc1_channel_t channel;
  esp_err_t err;
  int sample;

  FROTH_POP(pin);

  if (!esp32_adc1_channel_for_pin(pin, &channel)) {
    return FROTH_ERROR_BOUNDS;
  }

  err = adc1_config_width(ADC_WIDTH_BIT_12);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }

  err = adc1_config_channel_atten(channel, FROTH_BOARD_ADC_ATTEN);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }

  sample = adc1_get_raw(channel);
  if (sample < 0) {
    return FROTH_ERROR_IO;
  }

  FROTH_PUSH(sample);
  return FROTH_OK;
}

/*----------------- LEDC FUNCTIONS -----------------*/

FROTH_FFI_ARITY(esp32_ledc_timer_config, "ledc.timer-config",
                "( speed_mode timer freq resolution -- )", 4, 0,
                "LEDC timer configuration.") {
  FROTH_POP(resolution);
  FROTH_POP(freq);
  FROTH_POP(timer);
  FROTH_POP(speed_mode);
  esp_err_t err =
      ledc_timer_config(&(ledc_timer_config_t){.speed_mode = speed_mode,
                                               .timer_num = timer,
                                               .freq_hz = freq,
                                               .duty_resolution = resolution,
                                               .clk_cfg = LEDC_AUTO_CLK,
                                               .deconfigure = false});

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_ledc_channel_config, "ledc.channel-config",
                "( pin speed_mode channel timer duty -- )", 5, 0,
                "LEDC channel configuration") {
  FROTH_POP(duty);
  FROTH_POP(timer);
  FROTH_POP(channel);
  FROTH_POP(speed_mode);
  FROTH_POP(gpio_num);
  esp_err_t err = ledc_channel_config(&(ledc_channel_config_t){
      .speed_mode = speed_mode,
      .channel = channel,
      .timer_sel = timer,
      .gpio_num = gpio_num,
      .duty = duty,
      .hpoint = 0,
      .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
      .flags = {.output_invert = 0},
  });

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_ledc_set_duty, "ledc.set-duty",
                "( speed_mode channel duty -- )", 3, 0,
                "Set LEDC duty. Call ledc.update_duty after to apply.") {
  FROTH_POP(duty);
  FROTH_POP(channel);
  FROTH_POP(speed_mode);
  esp_err_t err = ledc_set_duty(speed_mode, channel, duty);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_ledc_update_duty, "ledc.update-duty",
                "( speed_mode channel -- )", 2, 0, "Apply LEDC duty change") {
  FROTH_POP(channel);
  FROTH_POP(speed_mode);
  esp_err_t err = ledc_update_duty(speed_mode, channel);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_ledc_get_duty, "ledc.get-duty",
                "( speed_mode channel -- duty )", 2, 1, "Get LEDC duty") {
  FROTH_POP(channel);
  FROTH_POP(speed_mode);
  froth_cell_t duty = ledc_get_duty(speed_mode, channel);

  if (duty == LEDC_ERR_DUTY) {
    return FROTH_ERROR_IO;
  }

  FROTH_PUSH(duty);
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_ledc_set_frequency, "ledc.set-freq",
                "( speed_mode timer freq -- )", 3, 0,
                "Set LEDC frequency. Call ledc.update_duty after to apply.") {
  FROTH_POP(freq);
  FROTH_POP(timer);
  FROTH_POP(speed_mode);
  esp_err_t err = ledc_set_freq(speed_mode, timer, freq);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_ledc_get_frequency, "ledc.get-freq",
                "( speed_mode timer -- freq )", 2, 1, "Get LEDC frequency") {
  FROTH_POP(timer);
  FROTH_POP(speed_mode);
  uint32_t freq = ledc_get_freq(speed_mode, timer);

  if (freq == 0) { // Error is explicitly considered an error.
    return FROTH_ERROR_IO;
  }

  FROTH_PUSH(freq);
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_ledc_stop, "ledc.stop",
                "( speed_mode channel idle_level -- )", 3, 0,
                "Stop LEDC output") {
  FROTH_POP(idle_level);
  FROTH_POP(channel);
  FROTH_POP(speed_mode);
  esp_err_t err = ledc_stop(speed_mode, channel, idle_level);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_ledc_fade_func_install, "ledc.fade-install", "( -- )", 0,
                0, "Install LEDC fade function") {
  esp_err_t err = ledc_fade_func_install(0);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_ledc_fade_func_uninstall, "ledc.fade-uninstall", "( -- )",
                0, 0, "Uninstall LEDC fade function") {
  ledc_fade_func_uninstall();

  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_ledc_fade_with_time, "ledc.fade-with-time",
                "( speed_mode channel target_duty time_ms -- )", 4, 0,
                "Start LEDC Fade.") {
  FROTH_POP(time_ms);
  FROTH_POP(target_duty);
  FROTH_POP(channel);
  FROTH_POP(speed_mode);
  esp_err_t err =
      ledc_set_fade_with_time(speed_mode, channel, target_duty, time_ms);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_ledc_fade_start, "ledc.fade-start",
                "( speed_mode channel fade_mode -- )", 3, 0,
                "Start LEDC Fade. Call ledc.update_duty after to apply.") {
  FROTH_POP(fade_mode);
  FROTH_POP(channel);
  FROTH_POP(speed_mode);
  esp_err_t err = ledc_fade_start(speed_mode, channel, fade_mode);

  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }
  return FROTH_OK;
}

/* -----------------  I2C BINDINGS --------------------- */

#define I2C_MAX_BUSES 2
#define I2C_MAX_DEVICES 8
static i2c_master_bus_handle_t bus_handles[I2C_MAX_BUSES];
static i2c_master_dev_handle_t dev_handles[I2C_MAX_DEVICES];

FROTH_FFI_ARITY(esp32_i2c_init, "i2c.init", "( sda scl freq -- bus )", 3, 1,
                "Initialize an I2C master bus. Returns a bus handle (0-1).") {
  FROTH_POP(freq);
  FROTH_POP(scl);
  FROTH_POP(sda);

  for (int i = 0; i < I2C_MAX_BUSES; i++) {
    if (bus_handles[i] != NULL)
      continue;

    i2c_master_bus_config_t config = {
        .i2c_port = -1,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {.enable_internal_pullup = 1, .allow_pd = 0},
    };

    i2c_master_bus_handle_t handle;
    esp_err_t err = i2c_new_master_bus(&config, &handle);
    if (err != ESP_OK)
      return FROTH_ERROR_IO;

    bus_handles[i] = handle;
    FROTH_PUSH(i);
    return FROTH_OK;
  }

  return FROTH_ERROR_BOUNDS; /* no free bus slot */
}

FROTH_FFI_ARITY(esp32_i2c_add_device, "i2c.add-device",
                "( bus addr speed -- device )", 3, 1,
                "Add an I2C device to a bus. Returns a device handle (0-7).") {
  FROTH_POP(speed);
  FROTH_POP(addr);
  FROTH_POP(bus);

  if (bus < 0 || bus >= I2C_MAX_BUSES || bus_handles[bus] == NULL)
    return FROTH_ERROR_BOUNDS;

  for (int i = 0; i < I2C_MAX_DEVICES; i++) {
    if (dev_handles[i] != NULL)
      continue;

    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = speed,
        .scl_wait_us = 0,
        .flags = {.disable_ack_check = 0},
    };

    i2c_master_dev_handle_t handle;
    esp_err_t err =
        i2c_master_bus_add_device(bus_handles[bus], &config, &handle);
    if (err != ESP_OK)
      return FROTH_ERROR_IO;

    dev_handles[i] = handle;
    FROTH_PUSH(i);
    return FROTH_OK;
  }

  return FROTH_ERROR_BOUNDS; /* no free device slot */
}

FROTH_FFI_ARITY(esp32_i2c_rm_device, "i2c.rm-device", "( device -- )", 1, 0,
                "Remove an I2C device and release its handle.") {
  FROTH_POP(idx);

  if (idx < 0 || idx >= I2C_MAX_DEVICES || dev_handles[idx] == NULL)
    return FROTH_ERROR_BOUNDS;

  esp_err_t err = i2c_master_bus_rm_device(dev_handles[idx]);
  dev_handles[idx] = NULL;
  if (err != ESP_OK)
    return FROTH_ERROR_IO;
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_i2c_del_bus, "i2c.del-bus", "( bus -- )", 1, 0,
                "Delete an I2C master bus and release its handle.") {
  FROTH_POP(idx);

  if (idx < 0 || idx >= I2C_MAX_BUSES || bus_handles[idx] == NULL)
    return FROTH_ERROR_BOUNDS;

  esp_err_t err = i2c_del_master_bus(bus_handles[idx]);
  bus_handles[idx] = NULL;
  if (err != ESP_OK)
    return FROTH_ERROR_IO;
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_i2c_probe, "i2c.probe", "( bus addr -- flag )", 2, 1,
                "Probe for a device at addr. Returns true (-1) or false (0).") {
  FROTH_POP(addr);
  FROTH_POP(bus);

  if (bus < 0 || bus >= I2C_MAX_BUSES || bus_handles[bus] == NULL)
    return FROTH_ERROR_BOUNDS;

  esp_err_t err = i2c_master_probe(bus_handles[bus], addr, 100);
  FROTH_PUSH(err == ESP_OK ? -1 : 0);
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_i2c_write_byte, "i2c.write-byte", "( device byte -- )", 2,
                0, "Transmit one byte to an I2C device.") {
  FROTH_POP(byte);
  FROTH_POP(dev);

  if (dev < 0 || dev >= I2C_MAX_DEVICES || dev_handles[dev] == NULL)
    return FROTH_ERROR_BOUNDS;

  uint8_t buf[1] = {(uint8_t)byte};
  esp_err_t err = i2c_master_transmit(dev_handles[dev], buf, 1, 1000);
  if (err != ESP_OK)
    return FROTH_ERROR_IO;
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_i2c_read_byte, "i2c.read-byte", "( device -- byte )", 1,
                1, "Receive one byte from an I2C device.") {
  FROTH_POP(dev);

  if (dev < 0 || dev >= I2C_MAX_DEVICES || dev_handles[dev] == NULL)
    return FROTH_ERROR_BOUNDS;

  uint8_t buf[1] = {0};
  esp_err_t err = i2c_master_receive(dev_handles[dev], buf, 1, 1000);
  if (err != ESP_OK)
    return FROTH_ERROR_IO;

  FROTH_PUSH(buf[0]);
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_i2c_write_reg, "i2c.write-reg", "( byte device reg -- )",
                3, 0, "Write a byte to a register on an I2C device.") {
  FROTH_POP(reg);
  FROTH_POP(dev);
  FROTH_POP(byte);

  if (dev < 0 || dev >= I2C_MAX_DEVICES || dev_handles[dev] == NULL)
    return FROTH_ERROR_BOUNDS;

  uint8_t buf[2] = {(uint8_t)reg, (uint8_t)byte};
  esp_err_t err = i2c_master_transmit(dev_handles[dev], buf, 2, 1000);
  if (err != ESP_OK)
    return FROTH_ERROR_IO;
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_i2c_read_reg, "i2c.read-reg", "( device reg -- byte )", 2,
                1, "Read one byte from a register on an I2C device.") {
  FROTH_POP(reg);
  FROTH_POP(dev);

  if (dev < 0 || dev >= I2C_MAX_DEVICES || dev_handles[dev] == NULL)
    return FROTH_ERROR_BOUNDS;

  uint8_t tx[1] = {(uint8_t)reg};
  uint8_t rx[1] = {0};
  esp_err_t err =
      i2c_master_transmit_receive(dev_handles[dev], tx, 1, rx, 1, 1000);
  if (err != ESP_OK)
    return FROTH_ERROR_IO;

  FROTH_PUSH(rx[0]);
  return FROTH_OK;
}

FROTH_FFI_ARITY(
    esp32_i2c_read_reg16, "i2c.read-reg16", "( device reg -- word )", 2, 1,
    "Read two bytes (big-endian) from a register on an I2C device.") {
  FROTH_POP(reg);
  FROTH_POP(dev);

  if (dev < 0 || dev >= I2C_MAX_DEVICES || dev_handles[dev] == NULL)
    return FROTH_ERROR_BOUNDS;

  uint8_t tx[1] = {(uint8_t)reg};
  uint8_t rx[2] = {0, 0};
  esp_err_t err =
      i2c_master_transmit_receive(dev_handles[dev], tx, 1, rx, 2, 1000);
  if (err != ESP_OK)
    return FROTH_ERROR_IO;

  froth_cell_t word = ((froth_cell_t)rx[0] << 8) | rx[1];
  FROTH_PUSH(word);
  return FROTH_OK;
}

/* ----------------- UART BINDINGS --------------------- */

#define UART_MAX_PORTS 2
static const uart_port_t uart_ports[UART_MAX_PORTS] = {UART_NUM_1, UART_NUM_2};
static uint8_t uart_in_use[UART_MAX_PORTS];
static int uart_tx_pins[UART_MAX_PORTS];
static int uart_rx_pins[UART_MAX_PORTS];

void froth_board_reset_runtime_state(void) {
  for (int i = 0; i < UART_MAX_PORTS; i++) {
    if (uart_in_use[i]) {
      (void)uart_driver_delete(uart_ports[i]);
    }
    uart_in_use[i] = 0;
    uart_tx_pins[i] = -1;
    uart_rx_pins[i] = -1;
  }
}

static bool aux_uart_conflicts_console(froth_cell_t tx, froth_cell_t rx) {
  platform_console_uart_info_t info;

  if (platform_console_uart_info(&info) != FROTH_OK) {
    return false;
  }

  return info.tx == tx || info.tx == rx || info.rx == tx || info.rx == rx;
}

static bool aux_uart_port_conflicts_console(int index) {
  platform_console_uart_info_t info;

  if (platform_console_uart_info(&info) != FROTH_OK) {
    return false;
  }

  return info.port == uart_ports[index];
}

static bool console_route_conflicts_aux(froth_cell_t port, froth_cell_t tx,
                                        froth_cell_t rx) {
  for (int i = 0; i < UART_MAX_PORTS; i++) {
    if (!uart_in_use[i]) {
      continue;
    }

    if ((froth_cell_t)uart_ports[i] == port) {
      return true;
    }

    if (uart_tx_pins[i] == tx || uart_tx_pins[i] == rx ||
        uart_rx_pins[i] == tx || uart_rx_pins[i] == rx) {
      return true;
    }
  }

  return false;
}

FROTH_FFI_ARITY(esp32_uart_init, "uart.init", "( tx rx baud -- uart )", 3, 1,
                "Initialize an auxiliary UART. Returns a UART handle (0-1).") {
  FROTH_POP(baud);
  FROTH_POP(rx);
  FROTH_POP(tx);

  if (aux_uart_conflicts_console(tx, rx)) {
    return FROTH_ERROR_BUSY;
  }

  uart_config_t config = {
      .baud_rate = (int)baud,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  for (int i = 0; i < UART_MAX_PORTS; i++) {
    if (aux_uart_port_conflicts_console(i)) {
      continue;
    }

    if (!uart_in_use[i]) {
      continue;
    }

    if (uart_tx_pins[i] != tx || uart_rx_pins[i] != rx) {
      continue;
    }

    esp_err_t err = uart_param_config(uart_ports[i], &config);
    if (err != ESP_OK) {
      return FROTH_ERROR_IO;
    }

    err = uart_flush_input(uart_ports[i]);
    if (err != ESP_OK) {
      return FROTH_ERROR_IO;
    }

    FROTH_PUSH(i);
    return FROTH_OK;
  }

  for (int i = 0; i < UART_MAX_PORTS; i++) {
    if (aux_uart_port_conflicts_console(i)) {
      continue;
    }

    if (uart_in_use[i]) {
      continue;
    }

    esp_err_t err = uart_driver_install(uart_ports[i], 256, 0, 0, NULL, 0);
    if (err != ESP_OK) {
      return FROTH_ERROR_IO;
    }

    err = uart_param_config(uart_ports[i], &config);
    if (err != ESP_OK) {
      uart_driver_delete(uart_ports[i]);
      return FROTH_ERROR_IO;
    }

    err = uart_set_pin(uart_ports[i], tx, rx, UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
      uart_driver_delete(uart_ports[i]);
      return FROTH_ERROR_IO;
    }

    err = uart_flush_input(uart_ports[i]);
    if (err != ESP_OK) {
      uart_driver_delete(uart_ports[i]);
      return FROTH_ERROR_IO;
    }

    uart_in_use[i] = 1;
    uart_tx_pins[i] = (int)tx;
    uart_rx_pins[i] = (int)rx;
    FROTH_PUSH(i);
    return FROTH_OK;
  }

  return FROTH_ERROR_BOUNDS;
}

FROTH_FFI_ARITY(esp32_console_info, "console.info", "( -- )", 0, 0,
                "Print the active console UART route.") {
  platform_console_uart_info_t info;

  FROTH_TRY(platform_console_uart_info(&info));

  FROTH_TRY(emit_string("console uart"));
  FROTH_TRY(emit_string(format_number(info.port)));
  FROTH_TRY(emit_string(" tx="));
  FROTH_TRY(emit_string(format_number(info.tx)));
  FROTH_TRY(emit_string(" rx="));
  FROTH_TRY(emit_string(format_number(info.rx)));
  FROTH_TRY(emit_string(" baud="));
  FROTH_TRY(emit_string(format_number(info.baud)));
  FROTH_TRY(emit_string("\n"));
  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_console_default, "console.default!", "( -- )", 0, 0,
                "Restore the default console UART route.") {
  if (froth_console_live_active()) {
    return FROTH_ERROR_BUSY;
  }

  if (console_route_conflicts_aux(FROTH_BOARD_CONSOLE_DEFAULT_PORT,
                                  FROTH_BOARD_CONSOLE_DEFAULT_TX_PIN,
                                  FROTH_BOARD_CONSOLE_DEFAULT_RX_PIN)) {
    return FROTH_ERROR_BUSY;
  }

  FROTH_TRY(froth_console_flush_output());
  return platform_console_uart_default();
}

FROTH_FFI_ARITY(esp32_console_uart_bind, "console.uart!",
                "( port tx rx baud -- )", 4, 0,
                "Rebind the active console to a UART route.") {
  FROTH_POP(baud);
  FROTH_POP(rx);
  FROTH_POP(tx);
  FROTH_POP(port);

  if (froth_console_live_active()) {
    return FROTH_ERROR_BUSY;
  }

  if (console_route_conflicts_aux(port, tx, rx)) {
    return FROTH_ERROR_BUSY;
  }

  FROTH_TRY(froth_console_flush_output());
  return platform_console_uart_bind(port, tx, rx, baud);
}

FROTH_FFI_ARITY(esp32_uart_write, "uart.write", "( byte uart -- )", 2, 0,
                "Write one byte to an auxiliary UART.") {
  FROTH_POP(uart);
  FROTH_POP(byte);

  if (uart < 0 || uart >= UART_MAX_PORTS || !uart_in_use[uart]) {
    return FROTH_ERROR_BOUNDS;
  }

  uint8_t out = (uint8_t)(byte & 0xff);
  int written = uart_write_bytes(uart_ports[uart], &out, 1);
  if (written != 1) {
    return FROTH_ERROR_IO;
  }

  return FROTH_OK;
}

FROTH_FFI_ARITY(esp32_uart_read, "uart.read", "( uart -- byte )", 1, 1,
                "Read one byte from an auxiliary UART.") {
  FROTH_POP(uart);

  if (uart < 0 || uart >= UART_MAX_PORTS || !uart_in_use[uart]) {
    return FROTH_ERROR_BOUNDS;
  }

  while (1) {
    uint8_t in = 0;
    int read = uart_read_bytes(uart_ports[uart], &in, 1, pdMS_TO_TICKS(10));
    if (read < 0) {
      return FROTH_ERROR_IO;
    }
    if (read == 1) {
      FROTH_PUSH(in);
      return FROTH_OK;
    }

    FROTH_TRY(poll_interruptible_wait(froth_vm));
  }
}

FROTH_FFI_ARITY(
    esp32_uart_available, "uart.key?", "( uart -- flag )", 1, 1,
    "Returns true (-1) if at least one byte is in RX buffer, (0) otherwise.") {
  FROTH_POP(uart);

  if (uart < 0 || uart >= UART_MAX_PORTS || !uart_in_use[uart]) {
    return FROTH_ERROR_BOUNDS;
  }

  size_t len = 0;
  froth_cell_t flag = 0;
  esp_err_t err = uart_get_buffered_data_len(uart_ports[uart], &len);
  if (err != ESP_OK) {
    return FROTH_ERROR_IO;
  }

  if (len > 0) {
    flag = -1;
  }

  FROTH_PUSH(flag);
  return FROTH_OK;
}

FROTH_BOARD_BEGIN(froth_board_bindings)
FROTH_BIND(esp32_gpio_mode), FROTH_BIND(esp32_gpio_read),
    FROTH_BIND(esp32_gpio_write), FROTH_BIND(esp32_ms),
    FROTH_BIND(esp32_adc_read),
    FROTH_BIND(esp32_ledc_timer_config), FROTH_BIND(esp32_ledc_channel_config),
    FROTH_BIND(esp32_ledc_set_duty), FROTH_BIND(esp32_ledc_update_duty),
    FROTH_BIND(esp32_ledc_get_duty), FROTH_BIND(esp32_ledc_set_frequency),
    FROTH_BIND(esp32_ledc_get_frequency), FROTH_BIND(esp32_ledc_stop),
    FROTH_BIND(esp32_ledc_fade_func_install),
    FROTH_BIND(esp32_ledc_fade_func_uninstall),
    FROTH_BIND(esp32_ledc_fade_with_time), FROTH_BIND(esp32_ledc_fade_start),
    FROTH_BIND(esp32_i2c_init), FROTH_BIND(esp32_i2c_add_device),
    FROTH_BIND(esp32_i2c_rm_device), FROTH_BIND(esp32_i2c_del_bus),
    FROTH_BIND(esp32_i2c_probe), FROTH_BIND(esp32_i2c_write_byte),
    FROTH_BIND(esp32_i2c_read_byte), FROTH_BIND(esp32_i2c_write_reg),
    FROTH_BIND(esp32_i2c_read_reg), FROTH_BIND(esp32_i2c_read_reg16),
    FROTH_BIND(esp32_console_info), FROTH_BIND(esp32_console_default),
    FROTH_BIND(esp32_console_uart_bind),
    FROTH_BIND(esp32_uart_init), FROTH_BIND(esp32_uart_write),
    FROTH_BIND(esp32_uart_read),
    FROTH_BIND(esp32_uart_available), FROTH_BOARD_END
