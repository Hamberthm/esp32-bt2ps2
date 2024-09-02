#ifndef __ESP32_PS2DEV_H__
#define __ESP32_PS2DEV_H__

#include <initializer_list>
#include <stack>

// #include "Arduino.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdint.h>
#include "scan_codes_set_2.h"
#include <nvs_flash.h>
#include <string>

namespace esp32_ps2dev
{
  // Time per clock should be 60 to 100 microseconds according to PS/2 specifications.
  // Thus, half period should be 30 to 50 microseconds.
  const uint32_t CLK_HALF_PERIOD_MICROS = 40;
  const uint32_t CLK_QUATER_PERIOD_MICROS = CLK_HALF_PERIOD_MICROS / 2;
  // I could not find any specification of time between bytes from the PS/2 specification.
  // Based on observation of the mouse signal waveform using an oscilloscope, there appears to be an interval of 1 to 2 clock cycles.
  // ref. https://youtu.be/UqRDLWGLCEk
  const uint32_t BYTE_INTERVAL_MICROS = 500; // in v0.4 was OK: 500, change if not working and you know what you're doing.
  const int PACKET_QUEUE_LENGTH = 20;
  const UBaseType_t DEFAULT_TASK_PRIORITY = 10;
  //const BaseType_t DEFAULT_TASK_CORE = APP_CPU_NUM;
  const BaseType_t DEFAULT_TASK_CORE = 0;
  const BaseType_t DEFAULT_TASK_CORE_MOUSE = 1;
  // The device should check for "HOST_REQUEST_TO_SEND" at a interval not exceeding 10 milliseconds.
  const uint32_t INTERVAL_CHECKING_HOST_SEND_REQUEST_MILLIS = 9;
  const uint32_t MOUSE_CLICK_PRESSING_DURATION_MILLIS = 100;

  class PS2Packet
  {
  public:
    uint8_t len;
    uint8_t data[16];
  };

  class PS2dev
  {
  public:
    PS2dev(int clk, int data);

    enum class BusState
    {
      IDLE,
      COMMUNICATION_INHIBITED,
      HOST_REQUEST_TO_SEND,
    };

    void config(UBaseType_t task_priority, BaseType_t task_core);
    void begin(BaseType_t core);
    int write(unsigned char data);
    int write_wait_idle(uint8_t data, uint64_t timeout_micros = 1500);
    int read(unsigned char *data, uint64_t timeout_ms = 0);
    virtual int reply_to_host(uint8_t host_cmd) = 0;
    BusState get_bus_state();
    SemaphoreHandle_t get_bus_mutex_handle();
    QueueHandle_t get_packet_queue_handle();
    int send_packet(PS2Packet *packet);

  protected:
    int _ps2clk;
    int _ps2data;
    UBaseType_t _config_task_priority = DEFAULT_TASK_PRIORITY;
    BaseType_t _config_task_core = DEFAULT_TASK_CORE;
    TaskHandle_t _task_process_host_request;
    TaskHandle_t _task_send_packet;
    QueueHandle_t _queue_packet;
    SemaphoreHandle_t _mutex_bus;
    void golo(int pin);
    void gohi(int pin);
    void ack();
  };

  class PS2Mouse : public PS2dev
  {
  public:
    PS2Mouse(int clk, int data);
    enum class ResolutionCode : uint8_t
    {
      RES_1 = 0x00,
      RES_2 = 0x01,
      RES_4 = 0x02,
      RES_8 = 0x03
    };
    enum class Scale : uint8_t
    {
      ONE_ONE = 0,
      TWO_ONE = 1
    };
    enum class Mode : uint8_t
    {
      REMOTE_MODE = 0,
      STREAM_MODE = 1,
      WRAP_MODE = 2
    };
    enum class Command : uint8_t
    {
      RESET = 0xFF,
      RESEND = 0xFE,
      ERROR = 0xFC,
      ACK = 0xFA,
      SET_DEFAULTS = 0xF6,
      DISABLE_DATA_REPORTING = 0xF5,
      ENABLE_DATA_REPORTING = 0xF4,
      SET_SAMPLE_RATE = 0xF3,
      GET_DEVICE_ID = 0xF2,
      SET_REMOTE_MODE = 0xF0,
      SET_WRAP_MODE = 0xEE,
      RESET_WRAP_MODE = 0xEC,
      READ_DATA = 0xEB,
      SET_STREAM_MODE = 0xEA,
      STATUS_REQUEST = 0xE9,
      SET_RESOLUTION = 0xE8,
      SET_SCALING_2_1 = 0xE7,
      SET_SCALING_1_1 = 0xE6,
      SELF_TEST_PASSED = 0xAA,
    };
    enum class Button : uint8_t
    {
      LEFT,
      RIGHT,
      MIDDLE,
      BUTTON_4,
      BUTTON_5,
    };

    void begin(bool restore_internal_state);
    int reply_to_host(uint8_t host_cmd);
    bool has_wheel();
    bool has_4th_and_5th_buttons();
    bool data_reporting_enabled();
    void reset_counter();
    uint8_t get_sample_rate();
    void move(int16_t x, int16_t y, int8_t wheel);
    void press(Button button);
    void release(Button button);
    void click(Button button);
    void _report();

  protected:
    static constexpr char const *TAG = "PS2Mouse";
    void _send_status();
    void _save_internal_state_to_nvs();
    void _load_internal_state_from_nvs();
    TaskHandle_t _task_poll_mouse_count;
    nvs_handle _nvs_handle;
    bool _has_wheel = false;
    bool _has_4th_and_5th_buttons = false;
    bool _data_reporting_enabled = false;
    ResolutionCode _resolution = ResolutionCode::RES_4;
    Scale _scale = Scale::ONE_ONE;
    Mode _mode = Mode::STREAM_MODE;
    Mode _last_mode = Mode::STREAM_MODE;
    uint8_t _last_sample_rate[3] = {0, 0, 0};
    uint8_t _sample_rate = 100;
    int16_t _count_x = 0;
    uint8_t _count_x_overflow = 0;
    int16_t _count_y = 0;
    uint8_t _count_y_overflow = 0;
    int8_t _count_z = 0;
    uint8_t _button_left = 0;
    uint8_t _button_right = 0;
    uint8_t _button_middle = 0;
    uint8_t _button_4th = 0;
    uint8_t _button_5th = 0;
  };

  class PS2Keyboard : public PS2dev
  {
  public:
    PS2Keyboard(int clk, int data);
    int reply_to_host(uint8_t host_cmd);
    enum class Command
    {
      RESET = 0xFF,
      RESEND = 0xFE,
      ACK = 0xFA,
      SET_DEFAULTS = 0xF6,
      DISABLE_DATA_REPORTING = 0xF5,
      ENABLE_DATA_REPORTING = 0xF4,
      SET_TYPEMATIC_RATE = 0xF3,
      GET_DEVICE_ID = 0xF2,
      SET_SCAN_CODE_SET = 0xF0,
      ECHO = 0xEE,
      SET_RESET_LEDS = 0xED,
      BAT_SUCCESS = 0xAA,
    };
    void begin();
    bool data_reporting_enabled();
    bool is_scroll_lock_led_on();
    bool is_num_lock_led_on();
    bool is_caps_lock_led_on();
    void keydown(scancodes::Key key);
    void keyup(scancodes::Key key);
    void type(scancodes::Key key);
    void type(std::initializer_list<scancodes::Key> keys);
    void type(const char *str);
    void keyHid_send(uint8_t btkey, bool keyDown);
    void keyHid_send_CCONTROL(uint16_t btkey, bool keyDown);

  protected:
    bool _data_reporting_enabled = true;
    bool _led_scroll_lock = false;
    bool _led_num_lock = false;
    bool _led_caps_lock = false;
  };

  void _taskfn_process_host_request(void *arg);
  void _taskfn_send_packet(void *arg);
  void _taskfn_poll_mouse_count(void *arg);

} // namespace esp32_ps2dev

#endif // __ESP32_PS2DEV_H__