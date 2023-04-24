#include "..\include\esp32-ps2dev.h"

namespace esp32_ps2dev {

PS2dev::PS2dev(int clk, int data) {
  _ps2clk = clk;
  _ps2data = data;
}

void PS2dev::config(UBaseType_t task_priority, BaseType_t task_core) {
  if (task_priority < 1) {
    task_priority = 1;
  } else if (task_priority > configMAX_PRIORITIES) {
    task_priority = configMAX_PRIORITIES - 1;
  }
  _config_task_priority = task_priority;
  _config_task_core = task_core;
}

void PS2dev::begin() {
  gohi(_ps2clk);
  gohi(_ps2data);
  _mutex_bus = xSemaphoreCreateMutex();
  _queue_packet = xQueueCreate(PACKET_QUEUE_LENGTH, sizeof(PS2Packet));
  xTaskCreateUniversal(_taskfn_process_host_request, "process_host_request", 4096, this, _config_task_priority, &_task_process_host_request,
                       _config_task_core);
  xTaskCreateUniversal(_taskfn_send_packet, "send_packet", 4096, this, _config_task_priority - 1, &_task_send_packet, _config_task_core);
}

void PS2dev::gohi(int pin) {
  digitalWrite(pin, HIGH);
  pinMode(pin, INPUT);
}
void PS2dev::golo(int pin) {
  pinMode(pin, OUTPUT_OPEN_DRAIN);
  digitalWrite(pin, LOW);
}
void PS2dev::ack() {
  delayMicroseconds(BYTE_INTERVAL_MICROS);
  write(0xFA);
  delayMicroseconds(BYTE_INTERVAL_MICROS);
}
int PS2dev::write(unsigned char data) {
  unsigned char i;
  unsigned char parity = 1;

  if (get_bus_state() != BusState::IDLE) {
    return -1;
  }

  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  taskENTER_CRITICAL(&mux);

  golo(_ps2data);
  delayMicroseconds(CLK_QUATER_PERIOD_MICROS);
  // device sends on falling clock
  golo(_ps2clk);  // start bit
  delayMicroseconds(CLK_HALF_PERIOD_MICROS);
  gohi(_ps2clk);
  delayMicroseconds(CLK_QUATER_PERIOD_MICROS);

  for (i = 0; i < 8; i++) {
    if (data & 0x01) {
      gohi(_ps2data);
    } else {
      golo(_ps2data);
    }
    delayMicroseconds(CLK_QUATER_PERIOD_MICROS);
    golo(_ps2clk);
    delayMicroseconds(CLK_HALF_PERIOD_MICROS);
    gohi(_ps2clk);
    delayMicroseconds(CLK_QUATER_PERIOD_MICROS);

    parity = parity ^ (data & 0x01);
    data = data >> 1;
  }
  // parity bit
  if (parity) {
    gohi(_ps2data);
  } else {
    golo(_ps2data);
  }
  delayMicroseconds(CLK_QUATER_PERIOD_MICROS);
  golo(_ps2clk);
  delayMicroseconds(CLK_HALF_PERIOD_MICROS);
  gohi(_ps2clk);
  delayMicroseconds(CLK_QUATER_PERIOD_MICROS);

  // stop bit
  gohi(_ps2data);
  delayMicroseconds(CLK_QUATER_PERIOD_MICROS);
  golo(_ps2clk);
  delayMicroseconds(CLK_HALF_PERIOD_MICROS);
  gohi(_ps2clk);
  delayMicroseconds(CLK_QUATER_PERIOD_MICROS);

  taskEXIT_CRITICAL(&mux);

  return 0;
}
int PS2dev::write_wait_idle(uint8_t data, uint64_t timeout_micros) {
  uint64_t start_time = micros();
  while (get_bus_state() != BusState::IDLE) {
    if (micros() - start_time > timeout_micros) {
      return -1;
    }
  }
  return write(data);
}
int PS2dev::read(unsigned char* value, uint64_t timeout_ms) {
  unsigned int data = 0x00;
  unsigned int bit = 0x01;

  unsigned char calculated_parity = 1;
  unsigned char received_parity = 0;

  // wait for data line to go low and clock line to go high (or timeout)
  unsigned long waiting_since = millis();
  while (get_bus_state() != BusState::HOST_REQUEST_TO_SEND) {
    if ((millis() - waiting_since) > timeout_ms) return -1;
    delay(1);
  }

  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  taskENTER_CRITICAL(&mux);

  delayMicroseconds(CLK_QUATER_PERIOD_MICROS);
  golo(_ps2clk);
  delayMicroseconds(CLK_HALF_PERIOD_MICROS);
  gohi(_ps2clk);
  delayMicroseconds(CLK_QUATER_PERIOD_MICROS);

  while (bit < 0x0100) {
    if (digitalRead(_ps2data) == HIGH) {
      data = data | bit;
      calculated_parity = calculated_parity ^ 1;
    } else {
      calculated_parity = calculated_parity ^ 0;
    }

    bit = bit << 1;

    delayMicroseconds(CLK_QUATER_PERIOD_MICROS);
    golo(_ps2clk);
    delayMicroseconds(CLK_HALF_PERIOD_MICROS);
    gohi(_ps2clk);
    delayMicroseconds(CLK_QUATER_PERIOD_MICROS);
  }
  // we do the delay at the end of the loop, so at this point we have
  // already done the delay for the parity bit

  // parity bit
  if (digitalRead(_ps2data) == HIGH) {
    received_parity = 1;
  }

  // stop bit
  delayMicroseconds(CLK_QUATER_PERIOD_MICROS);
  golo(_ps2clk);
  delayMicroseconds(CLK_HALF_PERIOD_MICROS);
  gohi(_ps2clk);
  delayMicroseconds(CLK_QUATER_PERIOD_MICROS);

  delayMicroseconds(CLK_QUATER_PERIOD_MICROS);
  golo(_ps2data);
  golo(_ps2clk);
  delayMicroseconds(CLK_HALF_PERIOD_MICROS);
  gohi(_ps2clk);
  delayMicroseconds(CLK_QUATER_PERIOD_MICROS);
  gohi(_ps2data);

  taskEXIT_CRITICAL(&mux);

  *value = data & 0x00FF;

  if (received_parity == calculated_parity) {
    return 0;
  } else {
    return -2;
  }
}
PS2dev::BusState PS2dev::get_bus_state() {
  if (digitalRead(_ps2clk) == LOW) {
    return BusState::COMMUNICATION_INHIBITED;
  } else if (digitalRead(_ps2data) == LOW) {
    return BusState::HOST_REQUEST_TO_SEND;
  } else {
    return BusState::IDLE;
  }
}
SemaphoreHandle_t PS2dev::get_bus_mutex_handle() { return _mutex_bus; }
QueueHandle_t PS2dev::get_packet_queue_handle() { return _queue_packet; }
int PS2dev::send_packet(PS2Packet* packet) { return (xQueueSend(_queue_packet, packet, 0) == pdTRUE) ? 0 : -1; }

PS2Mouse::PS2Mouse(int clk, int data) : PS2dev(clk, data) {}
void PS2Mouse::begin() {
  PS2dev::begin();

  xSemaphoreTake(_mutex_bus, portMAX_DELAY);
  delayMicroseconds(BYTE_INTERVAL_MICROS);
  while (write(0xAA) != 0) delay(200);
  delayMicroseconds(BYTE_INTERVAL_MICROS);
  while (write(0x00) != 0) delay(1);
  xSemaphoreGive(_mutex_bus);

  xTaskCreateUniversal(_taskfn_poll_mouse_count, "PS2Mouse", 4096, this, _config_task_priority - 1, &_task_poll_mouse_count,
                       _config_task_core);
}
int PS2Mouse::reply_to_host(uint8_t host_cmd) {
  uint8_t val;
  if (_mode == Mode::WRAP_MODE) {
    switch ((Command)host_cmd) {
      case Command::SET_WRAP_MODE:  // set wrap mode
#if defined(_ESP32_PS2DEV_DEBUG_)
        _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: (WRAP_MODE) Set wrap mode command received");
#endif
        ack();
        reset_counter();
        break;
      case Command::RESET_WRAP_MODE:  // reset wrap mode
#if defined(_ESP32_PS2DEV_DEBUG_)
        _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: (WRAP_MODE) Reset wrap mode command received");
#endif
        ack();
        reset_counter();
        _mode = _last_mode;
        break;
      default:
        write(host_cmd);
    }
    return 0;
  }

  switch ((Command)host_cmd) {
    case Command::RESET:  // reset
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: Reset command received");
#endif
      ack();
      // the while loop lets us wait for the host to be ready
      while (write(0xAA) != 0) delay(1);
      while (write(0x00) != 0) delay(1);
      _has_wheel = false;
      _has_4th_and_5th_buttons = false;
      _sample_rate = 100;
      _resolution = ResolutionCode::RES_4;
      _scale = Scale::ONE_ONE;
      _data_reporting_enabled = false;
      _mode = Mode::STREAM_MODE;
      reset_counter();
      break;
    case Command::RESEND:  // resend
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: Resend command received");
#endif
      ack();
      break;
    case Command::SET_DEFAULTS:  // set defaults
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: Set defaults command received");
#endif
      // enter stream mode
      ack();
      _sample_rate = 100;
      _resolution = ResolutionCode::RES_4;
      _scale = Scale::ONE_ONE;
      _data_reporting_enabled = false;
      _mode = Mode::STREAM_MODE;
      reset_counter();
      break;
    case Command::DISABLE_DATA_REPORTING:  // disable data reporting
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: Disable data reporting command received");
#endif
      ack();
      _data_reporting_enabled = false;
      reset_counter();
      break;
    case Command::ENABLE_DATA_REPORTING:  // enable data reporting
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: Enable data reporting command received");
#endif
      ack();
      _data_reporting_enabled = true;
      reset_counter();
      break;
    case Command::SET_SAMPLE_RATE:  // set sample rate
      ack();
      if (read(&val) == 0) {
        switch (val) {
          case 10:
          case 20:
          case 40:
          case 60:
          case 80:
          case 100:
          case 200:
            _sample_rate = val;
            _last_sample_rate[0] = _last_sample_rate[1];
            _last_sample_rate[1] = _last_sample_rate[2];
            _last_sample_rate[2] = val;
#if defined(_ESP32_PS2DEV_DEBUG_)
            _ESP32_PS2DEV_DEBUG_.print("Set sample rate command received: ");
            _ESP32_PS2DEV_DEBUG_.println(val);
#endif
            ack();
            break;

          default:
            break;
        }
        // _min_report_interval_us = 1000000 / sample_rate;
        reset_counter();
      }
      break;
    case Command::GET_DEVICE_ID:  // get device id
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: Get device id command received");
#endif
      ack();
      if (_last_sample_rate[0] == 200 && _last_sample_rate[1] == 100 && _last_sample_rate[2] == 80) {
        write(0x03);  // Intellimouse with wheel
#if defined(_ESP32_PS2DEV_DEBUG_)
        _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: Act as Intellimouse with wheel.");
#endif
        _has_wheel = true;
      } else if (_last_sample_rate[0] == 200 && _last_sample_rate[1] == 200 && _last_sample_rate[2] == 80 && _has_wheel == true) {
        write(0x04);  // Intellimouse with 4th and 5th buttons
#if defined(_ESP32_PS2DEV_DEBUG_)
        _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: Act as Intellimouse with 4th and 5th buttons.");
#endif
        _has_4th_and_5th_buttons = true;
      } else {
        write(0x00);  // Standard PS/2 mouse
#if defined(_ESP32_PS2DEV_DEBUG_)
        _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: Act as standard PS/2 mouse.");
#endif
        _has_wheel = false;
        _has_4th_and_5th_buttons = false;
      }
      reset_counter();
      break;
    case Command::SET_REMOTE_MODE:  // set remote mode
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: Set remote mode command received");
#endif
      ack();
      reset_counter();
      _mode = Mode::REMOTE_MODE;
      break;
    case Command::SET_WRAP_MODE:  // set wrap mode
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: Set wrap mode command received");
#endif
      ack();
      reset_counter();
      _last_mode = _mode;
      _mode = Mode::WRAP_MODE;
      break;
    case Command::RESET_WRAP_MODE:  // reset wrap mode
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: Reset wrap mode command received");
#endif
      ack();
      reset_counter();
      break;
    case Command::READ_DATA:  // read data
      ack();
      _report();
      reset_counter();
      break;
    case Command::SET_STREAM_MODE:  // set stream mode
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: Set stream mode command received");
#endif
      ack();
      reset_counter();
      break;
    case Command::STATUS_REQUEST:  // status request
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: Status request command received");
#endif
      ack();
      _send_status();
      break;
    case Command::SET_RESOLUTION:  // set resolution
      ack();
      if (read(&val) == 0 && val <= 3) {
        _resolution = (ResolutionCode)val;
#if defined(_ESP32_PS2DEV_DEBUG_)
        _ESP32_PS2DEV_DEBUG_.print("PS2Mouse::reply_to_host: Set resolution command received: ");
        _ESP32_PS2DEV_DEBUG_.println(val, HEX);
#endif
        ack();
        reset_counter();
      }
      break;
    case Command::SET_SCALING_2_1:  // set scaling 2:1
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: Set scaling 2:1 command received");
#endif
      ack();
      _scale = Scale::TWO_ONE;
      break;
    case Command::SET_SCALING_1_1:  // set scaling 1:1
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Mouse::reply_to_host: Set scaling 1:1 command received");
#endif
      ack();
      _scale = Scale::ONE_ONE;
      break;
    default:
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.print("PS2Mouse::reply_to_host: Unknown command received: ");
      _ESP32_PS2DEV_DEBUG_.println(host_cmd, HEX);
#endif
      break;
  }
  return 0;
}
bool PS2Mouse::has_wheel() { return _has_wheel; }
bool PS2Mouse::has_4th_and_5th_buttons() { return _has_4th_and_5th_buttons; }
bool PS2Mouse::data_reporting_enabled() { return _data_reporting_enabled; }
void PS2Mouse::reset_counter() {
  _count_x = 0;
  _count_y = 0;
  _count_z = 0;
  _count_x_overflow = 0;
  _count_y_overflow = 0;
}
uint8_t PS2Mouse::get_sample_rate() { return _sample_rate; }
void PS2Mouse::move(int16_t x, int16_t y, int8_t wheel) {
  _count_x += x;
  _count_y += y;
  _count_z += wheel;
  xTaskNotifyGive(_task_poll_mouse_count);
}
void PS2Mouse::press(Button button) {
  switch (button) {
    case Button::LEFT:
      _button_left = 1;
      break;
    case Button::RIGHT:
      _button_right = 1;
      break;
    case Button::MIDDLE:
      _button_middle = 1;
      break;
    case Button::BUTTON_4:
      _button_4th = 1;
      break;
    case Button::BUTTON_5:
      _button_5th = 1;
      break;
    default:
      break;
  }
  xTaskNotifyGive(_task_poll_mouse_count);
}
void PS2Mouse::release(Button button) {
  switch (button) {
    case Button::LEFT:
      _button_left = 0;
      break;
    case Button::RIGHT:
      _button_right = 0;
      break;
    case Button::MIDDLE:
      _button_middle = 0;
      break;
    case Button::BUTTON_4:
      _button_4th = 0;
      break;
    case Button::BUTTON_5:
      _button_5th = 0;
      break;
    default:
      break;
  }
  xTaskNotifyGive(_task_poll_mouse_count);
}
void PS2Mouse::click(Button button) {
  press(button);
  delay(MOUSE_CLICK_PRESSING_DURATION_MILLIS);
  release(button);
}
void PS2Mouse::_report() {
  PS2Packet packet;
  if (_scale == Scale::TWO_ONE) {
    int16_t* p[2] = {&_count_x, &_count_y};
    for (size_t i = 0; i < 2; i++) {
      boolean positive = *p[i] >= 0;
      uint16_t abs_value = positive ? *p[i] : -*p[i];
      switch (abs_value) {
        case 1:
          abs_value = 1;
          break;
        case 2:
          abs_value = 1;
          break;
        case 3:
          abs_value = 3;
          break;
        case 4:
          abs_value = 6;
          break;
        case 5:
          abs_value = 9;
          break;
        default:
          abs_value *= 2;
          break;
      }
      if (!positive) *p[i] = -abs_value;
    }
  }
  if (_count_x > 255) {
    _count_x_overflow = 1;
    _count_x = 255;
  } else if (_count_x < -255) {
    _count_x_overflow = 1;
    _count_x = -255;
  }
  if (_count_y > 255) {
    _count_y_overflow = 1;
    _count_y = 255;
  } else if (_count_y < -255) {
    _count_y_overflow = 1;
    _count_y = -255;
  }
  if (_count_z > 7) {
    _count_z = 7;
  } else if (_count_z < -8) {
    _count_z = -8;
  }

  packet.len = 3 + _has_wheel;
  packet.data[0] = (_button_left) | ((_button_right) << 1) | ((_button_middle) << 2) | (1 << 3) | ((_count_x < 0) << 4) |
                   ((_count_y < 0) << 5) | (_count_x_overflow << 6) | (_count_y_overflow << 7);
  packet.data[1] = _count_x & 0xFF;
  packet.data[2] = _count_y & 0xFF;
  if (_has_wheel && !_has_4th_and_5th_buttons) {
    packet.data[3] = _count_z & 0xFF;
  } else if (_has_wheel && _has_4th_and_5th_buttons) {
    packet.data[3] = (_count_z & 0x0F) | ((_button_4th) << 4) | ((_button_5th) << 5);
  }

  send_packet(&packet);
  reset_counter();
}
void PS2Mouse::_send_status() {
  PS2Packet packet;
  packet.len = 3;
  boolean mode = (_mode == Mode::REMOTE_MODE);
  packet.data[0] = (_button_right & 1) & ((_button_middle & 1) << 1) & ((_button_left & 1) << 2) & ((0) << 3) &
                   (((uint8_t)_scale & 1) << 4) & ((_data_reporting_enabled & 1) << 5) & ((mode & 1) << 6) & ((0) << 7);
  packet.data[1] = (uint8_t)_resolution;
  packet.data[2] = _sample_rate;
  send_packet(&packet);
}

PS2Keyboard::PS2Keyboard(int clk, int data) : PS2dev(clk, data) {}
void PS2Keyboard::begin() {
  PS2dev::begin();

  xSemaphoreTake(_mutex_bus, portMAX_DELAY);
  delayMicroseconds(BYTE_INTERVAL_MICROS);
  delay(200);
  write(0xAA);
  xSemaphoreGive(_mutex_bus);
}
bool PS2Keyboard::data_reporting_enabled() { return _data_reporting_enabled; }
bool PS2Keyboard::is_scroll_lock_led_on() { return _led_scroll_lock; }
bool PS2Keyboard::is_num_lock_led_on() { return _led_num_lock; }
bool PS2Keyboard::is_caps_lock_led_on() { return _led_caps_lock; }
int PS2Keyboard::reply_to_host(uint8_t host_cmd) {
  uint8_t val;
  switch ((Command)host_cmd) {
    case Command::RESET:  // reset
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Keyboard::reply_to_host: Reset command received");
#endif  // _ESP32_PS2DEV_DEBUG_
      // the while loop lets us wait for the host to be ready
      ack(); // ack() provides delay, some systems need it
      while (write((uint8_t)Command::BAT_SUCCESS) != 0) delay(1);
      _data_reporting_enabled = false;
      break;
    case Command::RESEND:  // resend
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Keyboard::reply_to_host: Resend command received");
#endif  // _ESP32_PS2DEV_DEBUG_
      ack();
      break;
    case Command::SET_DEFAULTS:  // set defaults
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Keyboard::reply_to_host: Set defaults command received");
#endif  // _ESP32_PS2DEV_DEBUG_
      // enter stream mode
      ack();
      break;
    case Command::DISABLE_DATA_REPORTING:  // disable data reporting
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Keyboard::reply_to_host: Disable data reporting command received");
#endif  // _ESP32_PS2DEV_DEBUG_
      _data_reporting_enabled = false;
      ack();
      break;
    case Command::ENABLE_DATA_REPORTING:  // enable data reporting
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Keyboard::reply_to_host: Enable data reporting command received");
#endif  // _ESP32_PS2DEV_DEBUG_
      _data_reporting_enabled = true;
      ack();
      break;
    case Command::SET_TYPEMATIC_RATE:  // set typematic rate
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Keyboard::reply_to_host: Set typematic rate command received");
#endif  // _ESP32_PS2DEV_DEBUG_
      ack();
      if (!read(&val)) ack();  // do nothing with the rate
      break;
    case Command::GET_DEVICE_ID:  // get device id
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Keyboard::reply_to_host: Get device id command received");
#endif  // _ESP32_PS2DEV_DEBUG_
      ack();
      while (write(0xAB) != 0) delay(1); // ensure ID gets writed, some hosts may be sensitive
      while (write(0x83) != 0) delay(1); // this is critical for combined ports (they decide mouse/kb on this)
      break;
    case Command::SET_SCAN_CODE_SET:  // set scan code set
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Keyboard::reply_to_host: Set scan code set command received");
#endif  // _ESP32_PS2DEV_DEBUG_
      ack();
      if (!read(&val)) ack();  // do nothing with the rate
      break;
    case Command::ECHO:  // echo
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Keyboard::reply_to_host: Echo command received");
#endif  // _ESP32_PS2DEV_DEBUG_
      delayMicroseconds(BYTE_INTERVAL_MICROS);
      write(0xEE);
      delayMicroseconds(BYTE_INTERVAL_MICROS);
      break;
    case Command::SET_RESET_LEDS:  // set/reset LEDs
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.println("PS2Keyboard::reply_to_host: Set/reset LEDs command received");
#endif  // _ESP32_PS2DEV_DEBUG_
      while (write(0xAF) != 0) delay(1);
      if (!read(&val)) {
         while (write(0xAF) != 0) delay(1);
        _led_scroll_lock = ((val & 1) != 0);
        _led_num_lock = ((val & 2) != 0);
        _led_caps_lock = ((val & 4) != 0);
      }
      return 1;
      break;
    default:
#if defined(_ESP32_PS2DEV_DEBUG_)
      _ESP32_PS2DEV_DEBUG_.print("PS2Keyboard::reply_to_host: Unknown command received: ");
      _ESP32_PS2DEV_DEBUG_.println(host_cmd, HEX);
#endif  // _ESP32_PS2DEV_DEBUG_
      break;
  }

  return 0;
}
void PS2Keyboard::keydown(scancodes::Key key) {
  if (!_data_reporting_enabled) return;
  PS2Packet packet;
  packet.len = scancodes::MAKE_CODES_LEN[key];
  for (uint8_t i = 0; i < packet.len; i++) {
    packet.data[i] = scancodes::MAKE_CODES[key][i];
  }
  send_packet(&packet);
}
void PS2Keyboard::keyup(scancodes::Key key) {
  if (!_data_reporting_enabled) return;
  PS2Packet packet;
  packet.len = scancodes::BREAK_CODES_LEN[key];
  for (uint8_t i = 0; i < packet.len; i++) {
    packet.data[i] = scancodes::BREAK_CODES[key][i];
  }
  send_packet(&packet);
}
void PS2Keyboard::type(scancodes::Key key) {
  keydown(key);
  delay(10);
  keyup(key);
}
void PS2Keyboard::type(std::initializer_list<scancodes::Key> keys) {
  std::stack<scancodes::Key> stack;
  for (auto key : keys) {
    keydown(key);
    stack.push(key);
    delay(10);
  }
  while (!stack.empty()) {
    keyup(stack.top());
    stack.pop();
    delay(10);
  }
}
void PS2Keyboard::type(const char* str) {
  size_t i = 0;
  while (str[i] != '\0') {
    char c = str[i];
    scancodes::Key key;
    bool shift = false;
    switch (c) {
      case '\b':
        key = scancodes::Key::K_BACKSPACE;
        break;
      case '\t':
        key = scancodes::Key::K_TAB;
        break;
      case '\r':
      case '\n':
        key = scancodes::Key::K_RETURN;
        break;
      case ' ':
        key = scancodes::Key::K_SPACE;
        break;
      case '!':
        shift = true;
        key = scancodes::Key::K_1;
        break;
      case '\"':
        shift = true;
        key = scancodes::Key::K_QUOTE;
        break;
      case '#':
        shift = true;
        key = scancodes::Key::K_3;
        break;
      case '$':
        shift = true;
        key = scancodes::Key::K_4;
        break;
      case '&':
        shift = true;
        key = scancodes::Key::K_7;
        break;
      case '\'':
        key = scancodes::Key::K_QUOTE;
        break;
      case '(':
        shift = true;
        key = scancodes::Key::K_9;
        break;
      case ')':
        shift = true;
        key = scancodes::Key::K_0;
        break;
      case '*':
        shift = true;
        key = scancodes::Key::K_8;
        break;
      case '+':
        shift = true;
        key = scancodes::Key::K_EQUALS;
        break;
      case ',':
        key = scancodes::Key::K_COMMA;
        break;
      case '-':
        key = scancodes::Key::K_MINUS;
        break;
      case '.':
        key = scancodes::Key::K_PERIOD;
        break;
      case '/':
        key = scancodes::Key::K_SLASH;
        break;
      case '0':
        key = scancodes::Key::K_0;
        break;
      case '1':
        key = scancodes::Key::K_1;
        break;
      case '2':
        key = scancodes::Key::K_2;
        break;
      case '3':
        key = scancodes::Key::K_3;
        break;
      case '4':
        key = scancodes::Key::K_4;
        break;
      case '5':
        key = scancodes::Key::K_5;
        break;
      case '6':
        key = scancodes::Key::K_6;
        break;
      case '7':
        key = scancodes::Key::K_7;
        break;
      case '8':
        key = scancodes::Key::K_8;
        break;
      case '9':
        key = scancodes::Key::K_9;
        break;
      case ':':
        shift = true;
        key = scancodes::Key::K_SEMICOLON;
        break;
      case ';':
        key = scancodes::Key::K_SEMICOLON;
        break;
      case '<':
        shift = true;
        key = scancodes::Key::K_COMMA;
        break;
      case '=':
        key = scancodes::Key::K_EQUALS;
        break;
      case '>':
        shift = true;
        key = scancodes::Key::K_PERIOD;
        break;
      case '\?':
        shift = true;
        key = scancodes::Key::K_SLASH;
        break;
      case '@':
        shift = true;
        key = scancodes::Key::K_2;
        break;
      case '[':
        key = scancodes::Key::K_LEFTBRACKET;
        break;
      case '\\':
        key = scancodes::Key::K_BACKSLASH;
        break;
      case ']':
        key = scancodes::Key::K_RIGHTBRACKET;
        break;
      case '^':
        shift = true;
        key = scancodes::Key::K_6;
        break;
      case '_':
        shift = true;
        key = scancodes::Key::K_MINUS;
        break;
      case '`':
        key = scancodes::Key::K_BACKQUOTE;
        break;
      case 'a':
        key = scancodes::Key::K_A;
        break;
      case 'b':
        key = scancodes::Key::K_B;
        break;
      case 'c':
        key = scancodes::Key::K_C;
        break;
      case 'd':
        key = scancodes::Key::K_D;
        break;
      case 'e':
        key = scancodes::Key::K_E;
        break;
      case 'f':
        key = scancodes::Key::K_F;
        break;
      case 'g':
        key = scancodes::Key::K_G;
        break;
      case 'h':
        key = scancodes::Key::K_H;
        break;
      case 'i':
        key = scancodes::Key::K_I;
        break;
      case 'j':
        key = scancodes::Key::K_J;
        break;
      case 'k':
        key = scancodes::Key::K_K;
        break;
      case 'l':
        key = scancodes::Key::K_L;
        break;
      case 'm':
        key = scancodes::Key::K_M;
        break;
      case 'n':
        key = scancodes::Key::K_N;
        break;
      case 'o':
        key = scancodes::Key::K_O;
        break;
      case 'p':
        key = scancodes::Key::K_P;
        break;
      case 'q':
        key = scancodes::Key::K_Q;
        break;
      case 'r':
        key = scancodes::Key::K_R;
        break;
      case 's':
        key = scancodes::Key::K_S;
        break;
      case 't':
        key = scancodes::Key::K_T;
        break;
      case 'u':
        key = scancodes::Key::K_U;
        break;
      case 'v':
        key = scancodes::Key::K_V;
        break;
      case 'w':
        key = scancodes::Key::K_W;
        break;
      case 'x':
        key = scancodes::Key::K_X;
        break;
      case 'y':
        key = scancodes::Key::K_Y;
        break;
      case 'z':
        key = scancodes::Key::K_Z;
        break;
      case 'A':
        shift = true;
        key = scancodes::Key::K_A;
        break;
      case 'B':
        shift = true;
        key = scancodes::Key::K_B;
        break;
      case 'C':
        shift = true;
        key = scancodes::Key::K_C;
        break;
      case 'D':
        shift = true;
        key = scancodes::Key::K_D;
        break;
      case 'E':
        shift = true;
        key = scancodes::Key::K_E;
        break;
      case 'F':
        shift = true;
        key = scancodes::Key::K_F;
        break;
      case 'G':
        shift = true;
        key = scancodes::Key::K_G;
        break;
      case 'H':
        shift = true;
        key = scancodes::Key::K_H;
        break;
      case 'I':
        shift = true;
        key = scancodes::Key::K_I;
        break;
      case 'J':
        shift = true;
        key = scancodes::Key::K_J;
        break;
      case 'K':
        shift = true;
        key = scancodes::Key::K_K;
        break;
      case 'L':
        shift = true;
        key = scancodes::Key::K_L;
        break;
      case 'M':
        shift = true;
        key = scancodes::Key::K_M;
        break;
      case 'N':
        shift = true;
        key = scancodes::Key::K_N;
        break;
      case 'O':
        shift = true;
        key = scancodes::Key::K_O;
        break;
      case 'P':
        shift = true;
        key = scancodes::Key::K_P;
        break;
      case 'Q':
        shift = true;
        key = scancodes::Key::K_Q;
        break;
      case 'R':
        shift = true;
        key = scancodes::Key::K_R;
        break;
      case 'S':
        shift = true;
        key = scancodes::Key::K_S;
        break;
      case 'T':
        shift = true;
        key = scancodes::Key::K_T;
        break;
      case 'U':
        shift = true;
        key = scancodes::Key::K_U;
        break;
      case 'V':
        shift = true;
        key = scancodes::Key::K_V;
        break;
      case 'W':
        shift = true;
        key = scancodes::Key::K_W;
        break;
      case 'X':
        shift = true;
        key = scancodes::Key::K_X;
        break;
      case 'Y':
        shift = true;
        key = scancodes::Key::K_Y;
        break;
      case 'Z':
        shift = true;
        key = scancodes::Key::K_Z;
        break;

      default:
        i++;
        continue;
        break;
    }
    if (shift) {
      keydown(scancodes::Key::K_LSHIFT);
      delay(10);
      type(key);
      delay(10);
      keyup(scancodes::Key::K_LSHIFT);
    } else {
      type(key);
    }
    i++;
  }
}

void _taskfn_process_host_request(void* arg) {
  PS2dev* ps2dev = (PS2dev*)arg;
  while (true) {
    xSemaphoreTake(ps2dev->get_bus_mutex_handle(), portMAX_DELAY);
    if (ps2dev->get_bus_state() == PS2dev::BusState::HOST_REQUEST_TO_SEND) {
      uint8_t host_cmd;
      if (ps2dev->read(&host_cmd) == 0) {
        ps2dev->reply_to_host(host_cmd);
      }
    }
    xSemaphoreGive(ps2dev->get_bus_mutex_handle());
    delay(INTERVAL_CHECKING_HOST_SEND_REQUEST_MILLIS);
  }
  vTaskDelete(NULL);
}
void _taskfn_send_packet(void* arg) {
  PS2dev* ps2dev = (PS2dev*)arg;
  while (true) {
    PS2Packet packet;
    if (xQueueReceive(ps2dev->get_packet_queue_handle(), &packet, portMAX_DELAY) == pdTRUE) {
      xSemaphoreTake(ps2dev->get_bus_mutex_handle(), portMAX_DELAY);
      delayMicroseconds(BYTE_INTERVAL_MICROS);
      for (int i = 0; i < packet.len; i++) {
        ps2dev->write_wait_idle(packet.data[i]);
        delayMicroseconds(BYTE_INTERVAL_MICROS);
      }
      xSemaphoreGive(ps2dev->get_bus_mutex_handle());
    }
  }
  vTaskDelete(NULL);
}
void _taskfn_poll_mouse_count(void* arg) {
  PS2Mouse* ps2mouse = (PS2Mouse*)arg;
  while (true) {
    xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
    if (ps2mouse->data_reporting_enabled()) {
      ps2mouse->_report();
    }
    delay(1000 / ps2mouse->get_sample_rate());
  }
  vTaskDelete(NULL);
}

void PS2Keyboard::keyHid_send(uint8_t btkey, bool keyDown) {
        scancodes::Key key;
        switch (btkey) {
        case 0x04:
            key = scancodes::Key::K_A;
            break;
        case 0x05:
            key = scancodes::Key::K_B;
            break;
        case 0x06:
            key = scancodes::Key::K_C;
            break;
        case 0x07:
            key = scancodes::Key::K_D;
            break;
        case 0x08:
            key = scancodes::Key::K_E;
            break;
        case 0x09:
            key = scancodes::Key::K_F;
            break;
        case 0x0A:
            key = scancodes::Key::K_G;
            break;
        case 0x0B:
            key = scancodes::Key::K_H;
            break;
        case 0x0C:
            key = scancodes::Key::K_I;
            break;
        case 0x0D:
            key = scancodes::Key::K_J;
            break;
        case 0x0E:
            key = scancodes::Key::K_K;
            break;
        case 0x0F:
            key = scancodes::Key::K_L;
            break;
        case 0x10:
            key = scancodes::Key::K_M;
            break;
        case 0x11:
            key = scancodes::Key::K_N;
            break;
        case 0x12:
            key = scancodes::Key::K_O;
            break;
        case 0x13:
            key = scancodes::Key::K_P;
            break;
        case 0x14:
            key = scancodes::Key::K_Q;
            break;
        case 0x15:
            key = scancodes::Key::K_R;
            break;
        case 0x16:
            key = scancodes::Key::K_S;
            break;
        case 0x17:
            key = scancodes::Key::K_T;
            break;
        case 0x18:
            key = scancodes::Key::K_U;
            break;
        case 0x19:
            key = scancodes::Key::K_V;
            break;
        case 0x1A:
            key = scancodes::Key::K_W;
            break;
        case 0x1B:
            key = scancodes::Key::K_X;
            break;
        case 0x1C:
            key = scancodes::Key::K_Y;
            break;
        case 0x1D:
            key = scancodes::Key::K_Z;
            break;
        case 0x1E:
            key = scancodes::Key::K_1;
            break;
        case 0x1F:
            key = scancodes::Key::K_2;
            break;
        case 0x20:
            key = scancodes::Key::K_3;
            break;
        case 0x21:
            key = scancodes::Key::K_4;
            break;
        case 0x22:
            key = scancodes::Key::K_5;
            break;
        case 0x23:
            key = scancodes::Key::K_6;
            break;
        case 0x24:
            key = scancodes::Key::K_7;
            break;
        case 0x25:
            key = scancodes::Key::K_8;
            break;
        case 0x26:
            key = scancodes::Key::K_9;
            break;
        case 0x27:
            key = scancodes::Key::K_0;
            break;
        case 0x28:
            key = scancodes::Key::K_RETURN;
            break;
        case 0x29:
            key = scancodes::Key::K_ESCAPE;
            break;
        case 0x2A:
            key = scancodes::Key::K_BACKSPACE;
            break;
        case 0x2B:
            key = scancodes::Key::K_TAB;
            break;
        case 0x2C:
            key = scancodes::Key::K_SPACE;
            break;
        case 0x2D:
            key = scancodes::Key::K_MINUS;
            break;
        case 0x2E:
            key = scancodes::Key::K_EQUALS;
            break;
        case 0x2F:
            key = scancodes::Key::K_LEFTBRACKET;
            break;
        case 0x30:
            key = scancodes::Key::K_RIGHTBRACKET;
            break;
        case 0x31:
            key = scancodes::Key::K_BACKSLASH;
            break;
        case 0x33:
            key = scancodes::Key::K_SEMICOLON;
            break;
        case 0x34:
            key = scancodes::Key::K_QUOTE;
            break;
        case 0x35:
            key = scancodes::Key::K_BACKQUOTE;
            break;
        case 0x36:
            key = scancodes::Key::K_COMMA;
            break;
        case 0x37:
            key = scancodes::Key::K_PERIOD;
            break;
        case 0x38:
            key = scancodes::Key::K_SLASH;
            break;
        case 0x39:
            key = scancodes::Key::K_CAPSLOCK;
            break;
        case 0x3A:
            key = scancodes::Key::K_F1;
            break;
        case 0x3B:
            key = scancodes::Key::K_F2;
            break;
        case 0x3C:
            key = scancodes::Key::K_F3;
            break;
        case 0x3D:
            key = scancodes::Key::K_F4;
            break;
        case 0x3E:
            key = scancodes::Key::K_F5;
            break;
        case 0x3F:
            key = scancodes::Key::K_F6;
            break;
        case 0x40:
            key = scancodes::Key::K_F7;
            break;
        case 0x41:
            key = scancodes::Key::K_F8;
            break;
        case 0x42:
            key = scancodes::Key::K_F9;
            break;
        case 0x43:
            key = scancodes::Key::K_F10;
            break;
        case 0x44:
            key = scancodes::Key::K_F11;
            break;
        case 0x45:
            key = scancodes::Key::K_F12;
            break;
        case 0x46:
            key = scancodes::Key::K_PRINT;
            break;
        case 0x47:
            key = scancodes::Key::K_SCROLLOCK;
            break;
        case 0x48:
            key = scancodes::Key::K_PAUSE;
            break;
        case 0x49:
            key = scancodes::Key::K_INSERT;
            break;
        case 0x4A:
            key = scancodes::Key::K_HOME;
            break;
        case 0x4B:
            key = scancodes::Key::K_PAGEUP;
            break;
        case 0x4C:
            key = scancodes::Key::K_DELETE;
            break;
        case 0x4D:
            key = scancodes::Key::K_END;
            break;
        case 0x4E:
            key = scancodes::Key::K_PAGEDOWN;
            break;
        case 0x4F:
            key = scancodes::Key::K_RIGHT;
            break;
        case 0x50:
            key = scancodes::Key::K_LEFT;
            break;
        case 0x51:
            key = scancodes::Key::K_DOWN;
            break;
        case 0x52:
            key = scancodes::Key::K_UP;
            break;
        case 0x53:
            key = scancodes::Key::K_NUMLOCK;
            break;
        case 0x54:
            key = scancodes::Key::K_KP_DIVIDE;
            break;
        case 0x55:
            key = scancodes::Key::K_KP_MULTIPLY;
            break;
        case 0x56:
            key = scancodes::Key::K_KP_MINUS;
            break;
        case 0x57:
            key = scancodes::Key::K_KP_PLUS;
            break;
        case 0x58:
            key = scancodes::Key::K_KP_ENTER;
            break;
        case 0x59:
            key = scancodes::Key::K_KP1;
            break;
        case 0x5A:
            key = scancodes::Key::K_KP2;
            break;
        case 0x5B:
            key = scancodes::Key::K_KP3;
            break;
        case 0x5C:
            key = scancodes::Key::K_KP4;
            break;
        case 0x5D:
            key = scancodes::Key::K_KP5;
            break;
        case 0x5E:
            key = scancodes::Key::K_KP6;
            break;
        case 0x5F:
            key = scancodes::Key::K_KP7;
            break;
        case 0x60:
            key = scancodes::Key::K_KP8;
            break;
        case 0x61:
            key = scancodes::Key::K_KP9;
            break;
        case 0x62:
            key = scancodes::Key::K_KP0;
            break;
        case 0x63:
            key = scancodes::Key::K_KP_PERIOD;
            break;
        case 0x65:
            key = scancodes::Key::K_MENU;
            break;
        case 0x66:
            key = scancodes::Key::K_ACPI_POWER;
            break;
        case 0x74:
            key = scancodes::Key::K_MEDIA_PLAY_PAUSE;
            break;
        case 0x78:
            key = scancodes::Key::K_MEDIA_STOP;
            break;
        case 0x7F:
            key = scancodes::Key::K_MEDIA_MUTE;
            break;
        case 0x80:
            key = scancodes::Key::K_MEDIA_VOLUME_UP;
            break;
        case 0x81:
            key = scancodes::Key::K_MEDIA_VOLUME_DOWN;
            break;
        case 0xE0:
            key = scancodes::Key::K_LCTRL;
            break;
        case 0xE1:
            key = scancodes::Key::K_LSHIFT;
            break;
        case 0xE2:
            key = scancodes::Key::K_LALT;
            break;
        case 0xE3:
            key = scancodes::Key::K_LSUPER;
            break;
        case 0xE4:
            key = scancodes::Key::K_RCTRL;
            break;
        case 0xE5:
            key = scancodes::Key::K_RSHIFT;
            break;
        case 0xE6:
            key = scancodes::Key::K_RALT;
            break;
        case 0xE7:
            key = scancodes::Key::K_RSUPER;
            break;

        default:
            return;
            break;
        }

        if (keyDown)
            keydown(key);
        else 
            keyup(key);
}

}  // namespace esp32_ps2dev
