// Copyright (c) 2020 Guy Turcotte
//
// MIT License. Look at file licenses.txt for details.
//
// -----
//
// Original code from the bluetooth/esp_hid_host example of ESP-IDF license:
//
// Copyright 2017-2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_hidh.h"
#include "esp_hid_common.h"
#include "esp_gap_bt_api.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_gatt_defs.h"
#include "esp_wifi.h"

#include <vector>
#include <map>

// #include "esp32-hal-bt.h"

class BTKeyboard
{
public:
  typedef void pid_handler(uint32_t code);

  const uint8_t KEY_CAPS_LOCK = 0x39;

  enum class KeyModifier : uint8_t
  {
    L_CTRL = 0x01,
    L_SHIFT = 0x02,
    L_ALT = 0x04,
    L_META = 0x08,
    R_CTRL = 0x10,
    R_SHIFT = 0x20,
    R_ALT = 0x40,
    R_META = 0x80
  };

  const uint8_t CTRL_MASK = ((uint8_t)KeyModifier::L_CTRL) | ((uint8_t)KeyModifier::R_CTRL);
  const uint8_t SHIFT_MASK = ((uint8_t)KeyModifier::L_SHIFT) | ((uint8_t)KeyModifier::R_SHIFT);
  const uint8_t ALT_MASK = ((uint8_t)KeyModifier::L_ALT) | ((uint8_t)KeyModifier::R_ALT);
  const uint8_t META_MASK = ((uint8_t)KeyModifier::L_META) | ((uint8_t)KeyModifier::R_META);

  static const uint8_t MAX_KEY_COUNT = 10; // Adjusted for normal humans with 10 fingers
  // static const uint8_t MAX_KEY_COUNT = 11; // Uncomment if also using dick
  // static const uint8_t MAX_KEY_COUNT = 5; // Uncomment if you're a known argentinian politician

  struct KeyInfo
  {
    KeyModifier modifier;
    uint8_t keys[MAX_KEY_COUNT];
  };

  struct KeyInfo_CCONTROL // container for 16-bit CCONTROL Usage Codes
  {
    uint16_t keys[MAX_KEY_COUNT];
  };

  struct Mouse_Control
  {
    int16_t mouse_x = 0;

    int16_t mouse_y = 0;

    int8_t mouse_w = 0;

    uint8_t mouse_buttons = 0;
  };

private:
  static constexpr char const *TAG = "BTKeyboard";

  static const esp_bt_mode_t HIDH_IDLE_MODE = (esp_bt_mode_t)0x00;
  static const esp_bt_mode_t HIDH_BLE_MODE = (esp_bt_mode_t)0x01;
  static const esp_bt_mode_t HIDH_BT_MODE = (esp_bt_mode_t)0x02;
  static const esp_bt_mode_t HIDH_BTDM_MODE = (esp_bt_mode_t)0x03;

#if CONFIG_BT_HID_HOST_ENABLED
#if CONFIG_BT_BLE_ENABLED
  static const esp_bt_mode_t HID_HOST_MODE = HIDH_BTDM_MODE;
#else
  static const esp_bt_mode_t HID_HOST_MODE = HIDH_BT_MODE;
#endif
#elif CONFIG_BT_BLE_ENABLED
  static const esp_bt_mode_t HID_HOST_MODE = HIDH_BLE_MODE;
#else
  static const esp_bt_mode_t HID_HOST_MODE = HIDH_IDLE_MODE;
#endif

  static SemaphoreHandle_t bt_hidh_cb_semaphore;
  static SemaphoreHandle_t ble_hidh_cb_semaphore;

  struct esp_hid_scan_result_t
  {
    struct esp_hid_scan_result_t *next;

    esp_bd_addr_t bda;
    const char *name;
    int8_t rssi;
    esp_hid_usage_t usage;
    esp_hid_transport_t transport; // BT, BLE or USB

    union
    {
      struct
      {
        esp_bt_cod_t cod;
        esp_bt_uuid_t uuid;
      } bt;
      struct
      {
        esp_ble_addr_type_t addr_type;
        uint16_t appearance;
      } ble;
    };
  };

  typedef struct esp_hidh_dev_report_s // stealed from stack
  {
    struct esp_hidh_dev_report_s *next;
    uint8_t map_index;     // the index of the report map
    uint8_t report_id;     // the id of the report
    uint8_t report_type;   // input, output or feature
    uint8_t protocol_mode; // boot or report
    size_t value_len;      // maximum len of value by report map
    esp_hid_usage_t usage; // generic, keyboard or mouse
    // BLE properties
    uint16_t handle;     // handle to the value
    uint16_t ccc_handle; // handle to client config
    uint8_t permissions; // report permissions
  } esp_hidh_dev_report_t;

  typedef struct
  {
    uint16_t usage_page = 0;
    uint16_t usage = 0;
    uint16_t inner_usage_page = 0;
    uint16_t inner_usage = 0;
    uint8_t report_id = 0;
    uint16_t input_len = 0;
    uint16_t output_len = 0;
    uint16_t feature_len = 0;
    uint32_t logical_minimum = 0;
    uint32_t logical_maximum = 0;
    uint32_t usage_minimum = 0;
    uint32_t usage_maximum = 0;
    bool contains_array = false;
    uint16_t mouse_x_bit_index = 0;
    uint16_t mouse_x_lenght = 0;
    uint16_t mouse_y_bit_index = 0;
    uint16_t mouse_y_lenght = 0;
    uint16_t mouse_w_bit_index = 0;
    uint16_t mouse_w_lenght = 0;
    uint16_t mouse_buttons_bit_index = 0;
    uint16_t mouse_buttons_amount = 0;
  } hid_report_params_t;

  typedef struct
  {
    uint8_t report_id = 0;
    uint16_t input_len = 0;
    uint32_t logical_minimum = 0;
    uint32_t logical_maximum = 0;
    uint32_t usage_minimum = 0;
    uint32_t usage_maximum = 0;
    uint16_t report_count = 0;
    bool contains_array = false;
    std::vector<uint16_t> array_usages;
  } hid_report_multimedia_control;

  typedef struct
  {
    uint8_t report_id = 0;
    uint16_t input_len = 0;
    uint16_t mouse_x_bit_index = 0;
    uint16_t mouse_x_bit_lenght = 0;
    uint16_t mouse_y_bit_index = 0;
    uint16_t mouse_y_bit_lenght = 0;
    uint16_t mouse_w_bit_index = 0;
    uint16_t mouse_w_bit_lenght = 0;
    uint16_t mouse_buttons_bit_index = 0;
    uint16_t mouse_buttons_amount = 0;
  } hid_report_mouse;

  static std::map<std::pair<esp_hidh_dev_t *, uint16_t>, hid_report_multimedia_control> multimedia_reports;
  static std::map<std::pair<esp_hidh_dev_t *, uint16_t>, hid_report_mouse> mouse_reports;
  static KeyInfo infoKey;

  typedef enum
  {
    PARSE_WAIT_USAGE_PAGE,
    PARSE_WAIT_USAGE,
    PARSE_WAIT_COLLECTION_APPLICATION,
    PARSE_WAIT_END_COLLECTION
  } s_parse_step_t;

  static s_parse_step_t s_parse_step;
  static uint8_t s_collection_depth;
  static hid_report_params_t s_report_params;
  static hid_report_params_t s_report_params_empty;
  static uint16_t s_report_size;
  static uint16_t s_report_count;
  static int s_usages_count;
  static std::vector<uint16_t> temp_usages_array;

  typedef struct
  {
    uint8_t cmd;
    uint8_t len;
    union
    {
      uint32_t value;
      uint8_t data[4];
    };
  } hid_report_cmd_t;

  esp_hid_scan_result_t *bt_scan_results;
  esp_hid_scan_result_t *ble_scan_results;
  static esp_hid_scan_result_t lastConnected;
  size_t num_bt_scan_results;
  size_t num_ble_scan_results;

  static void hidh_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data);

  static void bt_gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);
  static void ble_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

  static const char *ble_addr_type_str(esp_ble_addr_type_t ble_addr_type);
  static const char *ble_gap_evt_str(uint8_t event);
  static const char *bt_gap_evt_str(uint8_t event);
  static const char *ble_key_type_str(esp_ble_key_type_t key_type);

  static const char *gap_bt_prop_type_names[];
  static const char *ble_gap_evt_names[];
  static const char *bt_gap_evt_names[];
  static const char *ble_addr_type_names[];

  static const char shift_trans_dict[];

  void handle_bt_device_result(esp_bt_gap_cb_param_t *param);
  void handle_ble_device_result(esp_ble_gap_cb_param_t *scan_rst);

  void esp_hid_scan_results_free(esp_hid_scan_result_t *results);
  esp_hid_scan_result_t *find_scan_result(esp_bd_addr_t bda, esp_hid_scan_result_t *results);

  void add_bt_scan_result(esp_bd_addr_t bda,
                          esp_bt_cod_t *cod,
                          esp_bt_uuid_t *uuid,
                          uint8_t *name,
                          uint8_t name_len,
                          int rssi);

  void add_ble_scan_result(esp_bd_addr_t bda,
                           esp_ble_addr_type_t addr_type,
                           uint16_t appearance,
                           uint8_t *name,
                           uint8_t name_len,
                           int rssi);

  void print_uuid(esp_bt_uuid_t *uuid);

  esp_err_t start_ble_scan(uint32_t seconds);
  esp_err_t start_bt_scan(uint32_t seconds);
  esp_err_t esp_hid_scan(uint32_t seconds, size_t *num_results, esp_hid_scan_result_t **results, bool enable_bt_classic);

  inline void set_battery_level(uint8_t level) { battery_level = level; }

  void push_key(uint8_t *keys, uint8_t size);
  void push_key_CCONTROL(uint16_t *keys, uint8_t size);
  void mouse_handle(uint8_t *report_data, std::pair<esp_hidh_dev_t *, uint16_t> *key_pair);

  QueueHandle_t event_queue;
  QueueHandle_t event_queue_CCONTROL; // queue for long 16-bit CCONTROL Usage Codes
  QueueHandle_t event_queue_MOUSE;    // queue for mouse control
  int8_t battery_level;
  bool key_avail[MAX_KEY_COUNT];
  char last_ch;
  TickType_t repeat_period;
  pid_handler *pairing_handler;
  bool caps_lock;

  static esp_err_t hid_report_parse_multimedia_keys(const uint8_t *hid_rm, size_t hid_rm_len, esp_hidh_dev_t *device);
  static int parse_cmd(const uint8_t *data, size_t len, size_t index, hid_report_cmd_t **out);
  static int handle_cmd(hid_report_cmd_t *cmd, esp_hidh_dev_t *device);
  static int handle_report(hid_report_params_t *report, esp_hidh_dev_t *device);
  int16_t getBits(const void *Data, uint16_t StartBit, uint16_t NumBits);

public:
  BTKeyboard() : bt_scan_results(nullptr),
                 ble_scan_results(nullptr),
                 num_bt_scan_results(0),
                 num_ble_scan_results(0),
                 pairing_handler(nullptr),
                 caps_lock(false)
  {
  }

  bool setup(pid_handler *handler = nullptr);
  bool devices_scan(int seconds_wait_time = 5);
  bool devices_scan_ble_daemon(int seconds_wait_time = 5);

  inline uint8_t get_battery_level() { return battery_level; }

  inline bool wait_for_low_event(KeyInfo &inf, TickType_t duration = portMAX_DELAY)
  {
    return xQueueReceive(event_queue, &inf, duration);
  }

  inline bool wait_for_low_event_CCONTROL(KeyInfo_CCONTROL &inf, TickType_t duration = portMAX_DELAY)
  {
    return xQueueReceive(event_queue_CCONTROL, &inf, duration);
  }

  inline bool wait_for_low_event_MOUSE(Mouse_Control &inf, TickType_t duration = portMAX_DELAY)
  {
    return xQueueReceive(event_queue_MOUSE, &inf, duration);
  }

  char wait_for_ascii_char(bool forever = true);
  inline char get_ascii_char() { return wait_for_ascii_char(false); }

  void quick_reconnect(void);

  static bool isConnected; // hidh callback event CLOSE turns this false when kb disconnects
  static bool btFound;     // found a BT (not BLE) device during scan, let's wait for pairing
};
