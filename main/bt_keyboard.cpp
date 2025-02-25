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

#define __BT_KEYBOARD__ 1
#include "../include/bt_keyboard.hpp"

#include <cstring>
#include <algorithm>
#include <iterator>

#define SCAN 1

// uncomment to print all devices that were seen during a scan
#define GAP_DBG_PRINTF(...) printf(__VA_ARGS__)

#define SIZEOF_ARRAY(a) (sizeof(a) / sizeof(*a))

#define WAIT_BT_CB() xSemaphoreTake(bt_hidh_cb_semaphore, portMAX_DELAY)
#define SEND_BT_CB() xSemaphoreGive(bt_hidh_cb_semaphore)

#define WAIT_BLE_CB() xSemaphoreTake(ble_hidh_cb_semaphore, portMAX_DELAY)
#define SEND_BLE_CB() xSemaphoreGive(ble_hidh_cb_semaphore)

SemaphoreHandle_t BTKeyboard::bt_hidh_cb_semaphore = nullptr;
SemaphoreHandle_t BTKeyboard::ble_hidh_cb_semaphore = nullptr;

const char *BTKeyboard::gap_bt_prop_type_names[] = {"", "BDNAME", "COD", "RSSI", "EIR"};
const char *BTKeyboard::ble_gap_evt_names[] = {"ADV_DATA_SET_COMPLETE", "SCAN_RSP_DATA_SET_COMPLETE", "SCAN_PARAM_SET_COMPLETE", "SCAN_RESULT", "ADV_DATA_RAW_SET_COMPLETE", "SCAN_RSP_DATA_RAW_SET_COMPLETE", "ADV_START_COMPLETE", "SCAN_START_COMPLETE", "AUTH_CMPL", "KEY", "SEC_REQ", "PASSKEY_NOTIF", "PASSKEY_REQ", "OOB_REQ", "LOCAL_IR", "LOCAL_ER", "NC_REQ", "ADV_STOP_COMPLETE", "SCAN_STOP_COMPLETE", "SET_STATIC_RAND_ADDR", "UPDATE_CONN_PARAMS", "SET_PKT_LENGTH_COMPLETE", "SET_LOCAL_PRIVACY_COMPLETE", "REMOVE_BOND_DEV_COMPLETE", "CLEAR_BOND_DEV_COMPLETE", "GET_BOND_DEV_COMPLETE", "READ_RSSI_COMPLETE", "UPDATE_WHITELIST_COMPLETE"};
const char *BTKeyboard::bt_gap_evt_names[] = {"DISC_RES", "DISC_STATE_CHANGED", "RMT_SRVCS", "RMT_SRVC_REC", "AUTH_CMPL", "PIN_REQ", "CFM_REQ", "KEY_NOTIF", "KEY_REQ", "READ_RSSI_DELTA"};
const char *BTKeyboard::ble_addr_type_names[] = {"PUBLIC", "RANDOM", "RPA_PUBLIC", "RPA_RANDOM"};

const char BTKeyboard::shift_trans_dict[] =
    "aAbBcCdDeEfFgGhHiIjJkKlLmMnNoOpPqQrRsStTuUvVwWxXyYzZ1!2@3#4$5%6^7&8*9(0)"
    "\r\r\033\033\b\b\t\t  -_=+[{]}\\|??;:'\"`~,<.>/?"
    "\200\200"                                          // CAPS LOC
    "\201\201\202\202\203\203\204\204\205\205\206\206"  // F1..F6
    "\207\207\210\210\211\211\212\212\213\213\214\214"  // F7..F12
    "\215\215\216\216\217\217"                          // PrintScreen ScrollLock Pause
    "\220\220\221\221\222\222\177\177"                  // Insert Home PageUp Delete
    "\223\223\224\224\225\225\226\226\227\227\230\230"; // End PageDown Right Left Dow Up

static BTKeyboard *bt_keyboard = nullptr;
bool BTKeyboard::isConnected = false;
bool BTKeyboard::btFound = false;
BTKeyboard::esp_hid_scan_result_t BTKeyboard::lastConnected;

std::map<std::pair<esp_hidh_dev_t *, uint16_t>, BTKeyboard::hid_report_multimedia_control> BTKeyboard::multimedia_reports;
std::map<std::pair<esp_hidh_dev_t *, uint16_t>, BTKeyboard::hid_report_mouse> BTKeyboard::mouse_reports;
BTKeyboard::s_parse_step_t BTKeyboard::s_parse_step;
uint8_t BTKeyboard::s_collection_depth;
BTKeyboard::hid_report_params_t BTKeyboard::s_report_params;
BTKeyboard::hid_report_params_t BTKeyboard::s_report_params_empty;
uint16_t BTKeyboard::s_report_size;
uint16_t BTKeyboard::s_report_count;
int BTKeyboard::s_usages_count;
std::vector<uint16_t> BTKeyboard::temp_usages_array;
std::vector<uint16_t> multimedia_keys;

const char *
BTKeyboard::ble_addr_type_str(esp_ble_addr_type_t ble_addr_type)
{
  if (ble_addr_type > BLE_ADDR_TYPE_RPA_RANDOM)
  {
    return "UNKNOWN";
  }
  return ble_addr_type_names[ble_addr_type];
}

const char *
BTKeyboard::ble_gap_evt_str(uint8_t event)
{
  if (event >= SIZEOF_ARRAY(ble_gap_evt_names))
  {
    return "UNKNOWN";
  }
  return ble_gap_evt_names[event];
}

const char *
BTKeyboard::bt_gap_evt_str(uint8_t event)
{
  if (event >= SIZEOF_ARRAY(bt_gap_evt_names))
  {
    return "UNKNOWN";
  }
  return bt_gap_evt_names[event];
}

const char *
BTKeyboard::ble_key_type_str(esp_ble_key_type_t key_type)
{
  const char *key_str = nullptr;
  switch (key_type)
  {
  case ESP_LE_KEY_NONE:
    key_str = "ESP_LE_KEY_NONE";
    break;
  case ESP_LE_KEY_PENC:
    key_str = "ESP_LE_KEY_PENC";
    break;
  case ESP_LE_KEY_PID:
    key_str = "ESP_LE_KEY_PID";
    break;
  case ESP_LE_KEY_PCSRK:
    key_str = "ESP_LE_KEY_PCSRK";
    break;
  case ESP_LE_KEY_PLK:
    key_str = "ESP_LE_KEY_PLK";
    break;
  case ESP_LE_KEY_LLK:
    key_str = "ESP_LE_KEY_LLK";
    break;
  case ESP_LE_KEY_LENC:
    key_str = "ESP_LE_KEY_LENC";
    break;
  case ESP_LE_KEY_LID:
    key_str = "ESP_LE_KEY_LID";
    break;
  case ESP_LE_KEY_LCSRK:
    key_str = "ESP_LE_KEY_LCSRK";
    break;
  default:
    key_str = "INVALID BLE KEY TYPE";
    break;
  }

  return key_str;
}

void BTKeyboard::print_uuid(esp_bt_uuid_t *uuid)
{
  if (uuid->len == ESP_UUID_LEN_16)
  {
    GAP_DBG_PRINTF("UUID16: 0x%04x", uuid->uuid.uuid16);
  }
  else if (uuid->len == ESP_UUID_LEN_32)
  {
    GAP_DBG_PRINTF("UUID32: 0x%08x", uuid->uuid.uuid32);
  }
  else if (uuid->len == ESP_UUID_LEN_128)
  {
    GAP_DBG_PRINTF("UUID128: %02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x",
                   uuid->uuid.uuid128[0],
                   uuid->uuid.uuid128[1], uuid->uuid.uuid128[2], uuid->uuid.uuid128[3],
                   uuid->uuid.uuid128[4], uuid->uuid.uuid128[5], uuid->uuid.uuid128[6],
                   uuid->uuid.uuid128[7], uuid->uuid.uuid128[8], uuid->uuid.uuid128[9],
                   uuid->uuid.uuid128[10], uuid->uuid.uuid128[11], uuid->uuid.uuid128[12],
                   uuid->uuid.uuid128[13], uuid->uuid.uuid128[14], uuid->uuid.uuid128[15]);
  }
}

void BTKeyboard::esp_hid_scan_results_free(esp_hid_scan_result_t *results)
{
  esp_hid_scan_result_t *r = nullptr;
  while (results)
  {
    r = results;
    results = results->next;
    if (r->name != nullptr)
    {
      free((char *)r->name);
    }
    free(r);
  }
}

BTKeyboard::esp_hid_scan_result_t *
BTKeyboard::find_scan_result(esp_bd_addr_t bda, esp_hid_scan_result_t *results)
{
  esp_hid_scan_result_t *r = results;
  while (r)
  {
    if (memcmp(bda, r->bda, sizeof(esp_bd_addr_t)) == 0)
    {
      return r;
    }
    r = r->next;
  }
  return nullptr;
}

void BTKeyboard::add_bt_scan_result(esp_bd_addr_t bda,
                                    esp_bt_cod_t *cod,
                                    esp_bt_uuid_t *uuid,
                                    uint8_t *name,
                                    uint8_t name_len,
                                    int rssi)
{
  esp_hid_scan_result_t *r = find_scan_result(bda, bt_scan_results);
  if (r)
  {
    // Some info may come later
    if (r->name == nullptr && name && name_len)
    {
      char *name_s = (char *)malloc(name_len + 1);
      if (name_s == nullptr)
      {
        ESP_LOGE(TAG, "Malloc result name failed!");
        return;
      }
      memcpy(name_s, name, name_len);
      name_s[name_len] = 0;
      r->name = (const char *)name_s;
    }
    if (r->bt.uuid.len == 0 && uuid->len)
    {
      memcpy(&r->bt.uuid, uuid, sizeof(esp_bt_uuid_t));
    }
    if (rssi != 0)
    {
      r->rssi = rssi;
    }
    return;
  }

  r = (esp_hid_scan_result_t *)malloc(sizeof(esp_hid_scan_result_t));

  if (r == nullptr)
  {
    ESP_LOGE(TAG, "Malloc bt_hidh_scan_result_t failed!");
    return;
  }

  r->transport = ESP_HID_TRANSPORT_BT;

  memcpy(r->bda, bda, sizeof(esp_bd_addr_t));
  memcpy(&r->bt.cod, cod, sizeof(esp_bt_cod_t));
  memcpy(&r->bt.uuid, uuid, sizeof(esp_bt_uuid_t));

  r->usage = esp_hid_usage_from_cod((uint32_t)cod);
  r->rssi = rssi;
  r->name = nullptr;

  if (name_len && name)
  {
    char *name_s = (char *)malloc(name_len + 1);
    if (name_s == nullptr)
    {
      free(r);
      ESP_LOGE(TAG, "Malloc result name failed!");
      return;
    }
    memcpy(name_s, name, name_len);
    name_s[name_len] = 0;
    r->name = (const char *)name_s;
  }
  r->next = bt_scan_results;
  bt_scan_results = r;
  num_bt_scan_results++;
}

void BTKeyboard::add_ble_scan_result(esp_bd_addr_t bda,
                                     esp_ble_addr_type_t addr_type,
                                     uint16_t appearance,
                                     uint8_t *name,
                                     uint8_t name_len,
                                     int rssi)
{
  if (find_scan_result(bda, ble_scan_results))
  {
    ESP_LOGW(TAG, "Result already exists!");
    return;
  }

  esp_hid_scan_result_t *r = (esp_hid_scan_result_t *)malloc(sizeof(esp_hid_scan_result_t));

  if (r == nullptr)
  {
    ESP_LOGE(TAG, "Malloc ble_hidh_scan_result_t failed!");
    return;
  }

  r->transport = ESP_HID_TRANSPORT_BLE;

  memcpy(r->bda, bda, sizeof(esp_bd_addr_t));

  r->ble.appearance = appearance;
  r->ble.addr_type = addr_type;
  r->usage = esp_hid_usage_from_appearance(appearance);
  r->rssi = rssi;
  r->name = nullptr;

  if (name_len && name)
  {
    char *name_s = (char *)malloc(name_len + 1);
    if (name_s == nullptr)
    {
      free(r);
      ESP_LOGE(TAG, "Malloc result name failed!");
      return;
    }
    memcpy(name_s, name, name_len);
    name_s[name_len] = 0;
    r->name = (const char *)name_s;
  }

  r->next = ble_scan_results;
  ble_scan_results = r;
  num_ble_scan_results++;
}

bool BTKeyboard::setup(pid_handler *handler)
{
  esp_err_t ret;
  const esp_bt_mode_t mode = HID_HOST_MODE;

  s_parse_step = PARSE_WAIT_USAGE_PAGE;
  s_collection_depth = 0;
  s_report_params = {
      0,
  };
  s_report_size = 0;
  s_report_count = 0;
  s_usages_count = 0;
  temp_usages_array.clear();

  if (bt_keyboard != nullptr)
  {
    ESP_LOGE(TAG, "Setup called more than once. Only one instance of BTKeyboard is allowed.");
    return false;
  }

  bt_keyboard = this;

  pairing_handler = handler;
  event_queue = xQueueCreate(10, sizeof(KeyInfo));
  event_queue_CCONTROL = xQueueCreate(10, sizeof(KeyInfo_CCONTROL));
  event_queue_MOUSE = xQueueCreate(10, sizeof(Mouse_Control));

  if (HID_HOST_MODE == HIDH_IDLE_MODE)
  {
    ESP_LOGE(TAG, "Please turn on BT HID host or BLE!");
    return false;
  }

  bt_hidh_cb_semaphore = xSemaphoreCreateBinary();
  if (bt_hidh_cb_semaphore == nullptr)
  {
    ESP_LOGE(TAG, "xSemaphoreCreateMutex failed!");
    return false;
  }

  ble_hidh_cb_semaphore = xSemaphoreCreateBinary();
  if (ble_hidh_cb_semaphore == nullptr)
  {
    ESP_LOGE(TAG, "xSemaphoreCreateMutex failed!");
    vSemaphoreDelete(bt_hidh_cb_semaphore);
    bt_hidh_cb_semaphore = nullptr;
    return false;
  }

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

  bt_cfg.mode = mode;
  bt_cfg.bt_max_acl_conn = 3;
  bt_cfg.bt_max_sync_conn = 3;

  if ((ret = esp_bt_controller_init(&bt_cfg)))
  {
    ESP_LOGE(TAG, "esp_bt_controller_init failed: %d", ret);
    return false;
  }

  if ((ret = esp_bt_controller_enable(mode)))
  {
    ESP_LOGE(TAG, "esp_bt_controller_enable failed: %d", ret);
    return false;
  }

  if ((ret = esp_bluedroid_init()))
  {
    ESP_LOGE(TAG, "esp_bluedroid_init failed: %d", ret);
    return false;
  }

  if ((ret = esp_bluedroid_enable()))
  {
    ESP_LOGE(TAG, "esp_bluedroid_enable failed: %d", ret);
    return false;
  }

  // Classic Bluetooth GAP

  esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
  esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
  esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

  /*
   * Set default parameters for Legacy Pairing
   * Use fixed pin code
   */
  esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
  esp_bt_pin_code_t pin_code;
  pin_code[0] = '1';
  pin_code[1] = '2';
  pin_code[2] = '3';
  pin_code[3] = '4';
  esp_bt_gap_set_pin(pin_type, 4, pin_code);

  if ((ret = esp_bt_gap_register_callback(bt_gap_event_handler)))
  {
    ESP_LOGE(TAG, "esp_bt_gap_register_callback failed: %d", ret);
    return false;
  }

  // Allow BT devices to connect back to us
  if ((ret = esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE)))
  {
    ESP_LOGE(TAG, "esp_bt_gap_set_scan_mode failed: %d", ret);
    return false;
  }

  // BLE GAP

  if ((ret = esp_ble_gap_register_callback(ble_gap_event_handler)))
  {
    ESP_LOGE(TAG, "esp_ble_gap_register_callback failed: %d", ret);
    return false;
  }

  ESP_ERROR_CHECK(esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler));
  esp_hidh_config_t config = {
      .callback = hidh_callback,
      .event_stack_size = 4 * 1024, // Required with ESP-IDF 4.4
      .callback_arg = nullptr       // idem
  };
  ESP_ERROR_CHECK(esp_hidh_init(&config));

  /* set the security iocap & auth_req & key size & init key response key parameters to the stack*/
  esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_MITM_BOND; // bonding with peer device after authentication
  uint8_t key_size = 16;                                      // the key size should be 7~16 bytes
  uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  uint8_t oob_support = ESP_BLE_OOB_DISABLE;
  if (esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(uint8_t)) != ESP_OK)
    ESP_LOGE(TAG, "esp_ble_gap_set_security_param() failed on setting param: auth_req");
  if (esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(uint8_t)) != ESP_OK)
    ESP_LOGE(TAG, "esp_ble_gap_set_security_param() failed on setting param: iocap");
  if (esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(uint8_t)) != ESP_OK)
    ESP_LOGE(TAG, "esp_ble_gap_set_security_param() failed on setting param: key_size");
  if (esp_ble_gap_set_security_param(ESP_BLE_SM_OOB_SUPPORT, &oob_support, sizeof(uint8_t)) != ESP_OK)
    ESP_LOGE(TAG, "esp_ble_gap_set_security_param() failed on setting param: oob_support");
  /* If your BLE device act as a Slave, the init_key means you hope which types of key of the master should distribute to you,
  and the response key means which key you can distribute to the Master;
  If your BLE device act as a master, the response key means you hope which types of key of the slave should distribute to you,
  and the init key means which key you can distribute to the slave. */
  if (esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(uint8_t)) != ESP_OK)
    ESP_LOGE(TAG, "esp_ble_gap_set_security_param() failed on setting param: init_key");
  if (esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(uint8_t)) != ESP_OK)
    ESP_LOGE(TAG, "esp_ble_gap_set_security_param() failed on setting param: rsp_key");

  for (int i = 0; i < MAX_KEY_COUNT; i++)
  {
    key_avail[i] = true;
  }

  last_ch = 0;
  battery_level = -1;

  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN, ESP_PWR_LVL_P9);

  int pwrAdv = esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_ADV);
  int pwrScan = esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_SCAN);
  int pwrDef = esp_ble_tx_power_get(ESP_BLE_PWR_TYPE_DEFAULT);
  ESP_LOGI("BT_keyboard setup:", "Power Settings: (ADV,SCAN,DEFAULT) %u, %u, %u", pwrAdv, pwrScan, pwrDef); // all should show index7, aka +9dbm
  return true;
}

void BTKeyboard::handle_bt_device_result(esp_bt_gap_cb_param_t *param)
{
  GAP_DBG_PRINTF("BT : " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(param->disc_res.bda));

  uint32_t codv = 0;
  esp_bt_cod_t *cod = (esp_bt_cod_t *)&codv;
  int8_t rssi = 0;
  uint8_t *name = nullptr;
  uint8_t name_len = 0;
  esp_bt_uuid_t uuid;

  uuid.len = ESP_UUID_LEN_16;
  uuid.uuid.uuid16 = 0;

  for (int i = 0; i < param->disc_res.num_prop; i++)
  {
    esp_bt_gap_dev_prop_t *prop = &param->disc_res.prop[i];
    if (prop->type != ESP_BT_GAP_DEV_PROP_EIR)
    {
      GAP_DBG_PRINTF(", %s: ", gap_bt_prop_type_names[prop->type]);
    }
    if (prop->type == ESP_BT_GAP_DEV_PROP_BDNAME)
    {
      name = (uint8_t *)prop->val;
      name_len = strlen((const char *)name);
      GAP_DBG_PRINTF("%s", (const char *)name);
    }
    else if (prop->type == ESP_BT_GAP_DEV_PROP_RSSI)
    {
      rssi = *((int8_t *)prop->val);
      GAP_DBG_PRINTF("%d", rssi);
    }
    else if (prop->type == ESP_BT_GAP_DEV_PROP_COD)
    {
      memcpy(&codv, prop->val, sizeof(uint32_t));
      GAP_DBG_PRINTF("major: %s, minor: %d, service: 0x%03x", esp_hid_cod_major_str(cod->major), cod->minor, cod->service);
    }
    else if (prop->type == ESP_BT_GAP_DEV_PROP_EIR)
    {
      uint8_t len = 0;
      uint8_t *data = 0;

      data = esp_bt_gap_resolve_eir_data((uint8_t *)prop->val, ESP_BT_EIR_TYPE_CMPL_16BITS_UUID, &len);

      if (data == nullptr)
      {
        data = esp_bt_gap_resolve_eir_data((uint8_t *)prop->val, ESP_BT_EIR_TYPE_INCMPL_16BITS_UUID, &len);
      }

      if (data && len == ESP_UUID_LEN_16)
      {
        uuid.len = ESP_UUID_LEN_16;
        uuid.uuid.uuid16 = data[0] + (data[1] << 8);
        GAP_DBG_PRINTF(", ");
        print_uuid(&uuid);
        continue;
      }

      data = esp_bt_gap_resolve_eir_data((uint8_t *)prop->val, ESP_BT_EIR_TYPE_CMPL_32BITS_UUID, &len);

      if (data == nullptr)
      {
        data = esp_bt_gap_resolve_eir_data((uint8_t *)prop->val, ESP_BT_EIR_TYPE_INCMPL_32BITS_UUID, &len);
      }

      if (data && len == ESP_UUID_LEN_32)
      {
        uuid.len = len;
        memcpy(&uuid.uuid.uuid32, data, sizeof(uint32_t));
        GAP_DBG_PRINTF(", ");
        print_uuid(&uuid);
        continue;
      }

      data = esp_bt_gap_resolve_eir_data((uint8_t *)prop->val, ESP_BT_EIR_TYPE_CMPL_128BITS_UUID, &len);

      if (data == nullptr)
      {
        data = esp_bt_gap_resolve_eir_data((uint8_t *)prop->val, ESP_BT_EIR_TYPE_INCMPL_128BITS_UUID, &len);
      }

      if (data && len == ESP_UUID_LEN_128)
      {
        uuid.len = len;
        memcpy(uuid.uuid.uuid128, (uint8_t *)data, len);
        GAP_DBG_PRINTF(", ");
        print_uuid(&uuid);
        continue;
      }

      // try to find a name
      if (name == nullptr)
      {
        data = esp_bt_gap_resolve_eir_data((uint8_t *)prop->val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len);

        if (data == nullptr)
        {
          data = esp_bt_gap_resolve_eir_data((uint8_t *)prop->val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &len);
        }

        if (data && len)
        {
          name = data;
          name_len = len;
          GAP_DBG_PRINTF(", NAME: ");
          for (int x = 0; x < len; x++)
          {
            GAP_DBG_PRINTF("%c", (char)data[x]);
          }
        }
      }
    }
  }
  GAP_DBG_PRINTF("\n");

  if ((cod->major == ESP_BT_COD_MAJOR_DEV_PERIPHERAL) ||
      (find_scan_result(param->disc_res.bda, bt_scan_results) != nullptr))
  {
    add_bt_scan_result(param->disc_res.bda, cod, &uuid, name, name_len, rssi);
  }
}

void BTKeyboard::bt_gap_event_handler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
  switch (event)
  {
  case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
  {
    ESP_LOGV(TAG, "BT GAP DISC_STATE %s", (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) ? "START" : "STOP");
    if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED)
    {
      SEND_BT_CB();
    }
    break;
  }
  case ESP_BT_GAP_DISC_RES_EVT:
  {
    bt_keyboard->handle_bt_device_result(param);
    break;
  }
  case ESP_BT_GAP_KEY_NOTIF_EVT:
    ESP_LOGV(TAG, "BT GAP KEY_NOTIF passkey:%d", param->key_notif.passkey);
    if (bt_keyboard->pairing_handler != nullptr)
      (*bt_keyboard->pairing_handler)(param->key_notif.passkey);
    break;
  case ESP_BT_GAP_MODE_CHG_EVT:
    ESP_LOGV(TAG, "BT GAP MODE_CHG_EVT mode:%d", param->mode_chg.mode);
    break;
  default:
    ESP_LOGV(TAG, "BT GAP EVENT %s", bt_gap_evt_str(event));
    break;
  }
}

void BTKeyboard::handle_ble_device_result(esp_ble_gap_cb_param_t *param)
{
  uint16_t uuid = 0;
  uint16_t appearance = 0;
  char name[64] = "";

  uint8_t uuid_len = 0;
  uint8_t *uuid_d = esp_ble_resolve_adv_data(param->scan_rst.ble_adv, ESP_BLE_AD_TYPE_16SRV_CMPL, &uuid_len);

  if (uuid_d != nullptr && uuid_len)
  {
    uuid = uuid_d[0] + (uuid_d[1] << 8);
  }

  uint8_t appearance_len = 0;
  uint8_t *appearance_d = esp_ble_resolve_adv_data(param->scan_rst.ble_adv, ESP_BLE_AD_TYPE_APPEARANCE, &appearance_len);

  if (appearance_d != nullptr && appearance_len)
  {
    appearance = appearance_d[0] + (appearance_d[1] << 8);
  }

  uint8_t adv_name_len = 0;
  uint8_t *adv_name = esp_ble_resolve_adv_data(param->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &adv_name_len);

  if (adv_name == nullptr)
  {
    adv_name = esp_ble_resolve_adv_data(param->scan_rst.ble_adv, ESP_BLE_AD_TYPE_NAME_SHORT, &adv_name_len);
  }

  if (adv_name != nullptr && adv_name_len)
  {
    memcpy(name, adv_name, adv_name_len);
    name[adv_name_len] = 0;
  }

  GAP_DBG_PRINTF("BLE: " ESP_BD_ADDR_STR ", ", ESP_BD_ADDR_HEX(param->scan_rst.bda));
  GAP_DBG_PRINTF("RSSI: %d, ", param->scan_rst.rssi);
  GAP_DBG_PRINTF("UUID: 0x%04x, ", uuid);
  GAP_DBG_PRINTF("APPEARANCE: 0x%04x, ", appearance);
  GAP_DBG_PRINTF("ADDR_TYPE: '%s'", ble_addr_type_str(param->scan_rst.ble_addr_type));

  if (adv_name_len)
  {
    GAP_DBG_PRINTF(", NAME: '%s'", name);
  }
  GAP_DBG_PRINTF("\n");

  int numBonded = esp_ble_get_bond_device_num();
  esp_ble_bond_dev_t *dev_list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * numBonded); // bonded device information list

  if (numBonded > 0)
  {
    if ((esp_ble_get_bond_device_list(&numBonded, dev_list)) != ESP_OK)
    { // populate list
      ESP_LOGE(TAG, "esp_ble_get_bond_device_list failed");
      numBonded = 0; // this prevents the code below from trying to read the list
    }
  }

#if SCAN

  bool isLastBonded = false;

  for (int j = 0; (j < numBonded) && !isLastBonded; j++)
  {
    for (int i = 0; i < ESP_BD_ADDR_LEN; i++)
    {
      if (param->scan_rst.bda[i] == dev_list[j].bd_addr[i])
        isLastBonded = true;
      else
      {
        isLastBonded = false;
        break;
      }
    }
  }

  if (uuid == ESP_GATT_UUID_HID_SVC || isLastBonded == true)
  {
    add_ble_scan_result(param->scan_rst.bda,
                        param->scan_rst.ble_addr_type,
                        appearance, adv_name, adv_name_len,
                        param->scan_rst.rssi);
  }

#endif

  free(dev_list);
}

void BTKeyboard::ble_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
  switch (event)
  {

    // SCAN

  case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
  {
    ESP_LOGV(TAG, "BLE GAP EVENT SCAN_PARAM_SET_COMPLETE");
    SEND_BLE_CB();
    break;
  }
  case ESP_GAP_BLE_SCAN_RESULT_EVT:
  {
    switch (param->scan_rst.search_evt)
    {
    case ESP_GAP_SEARCH_INQ_RES_EVT:
    {
      bt_keyboard->handle_ble_device_result(param);
      break;
    }
    case ESP_GAP_SEARCH_INQ_CMPL_EVT:
      ESP_LOGV(TAG, "BLE GAP EVENT SCAN DONE: %d", param->scan_rst.num_resps);
      SEND_BLE_CB();
      break;
    default:
      break;
    }
    break;
  }
  case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
  {
    ESP_LOGV(TAG, "BLE GAP EVENT SCAN CANCELED");
    break;
  }

    // ADVERTISEMENT

  case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
    ESP_LOGV(TAG, "BLE GAP ADV_DATA_SET_COMPLETE");
    break;

  case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
    ESP_LOGV(TAG, "BLE GAP ADV_START_COMPLETE");
    break;

    // AUTHENTICATION

  case ESP_GAP_BLE_AUTH_CMPL_EVT:
    if (!param->ble_security.auth_cmpl.success)
    {
      ESP_LOGE(TAG, "BLE GAP AUTH ERROR: 0x%x", param->ble_security.auth_cmpl.fail_reason);
    }
    else
    {
      ESP_LOGV(TAG, "BLE GAP AUTH SUCCESS");
    }
    break;

  case ESP_GAP_BLE_KEY_EVT: // shows the ble key info share with peer device to the user.
    ESP_LOGV(TAG, "BLE GAP KEY type = %s", ble_key_type_str(param->ble_security.ble_key.key_type));
    break;

  case ESP_GAP_BLE_PASSKEY_NOTIF_EVT: // ESP_IO_CAP_OUT
    // The app will receive this evt when the IO has Output capability and the peer device IO has Input capability.
    // Show the passkey number to the user to input it in the peer device.
    ESP_LOGV(TAG, "BLE GAP PASSKEY_NOTIF passkey:%d", param->ble_security.key_notif.passkey);
    if (bt_keyboard->pairing_handler != nullptr)
      (*bt_keyboard->pairing_handler)(param->ble_security.key_notif.passkey);
    break;

  case ESP_GAP_BLE_NC_REQ_EVT: // ESP_IO_CAP_IO
    // The app will receive this event when the IO has DisplayYesNO capability and the peer device IO also has DisplayYesNo capability.
    // show the passkey number to the user to confirm it with the number displayed by peer device.
    ESP_LOGV(TAG, "BLE GAP NC_REQ passkey:%d", param->ble_security.key_notif.passkey);
    esp_ble_confirm_reply(param->ble_security.key_notif.bd_addr, true);
    break;

  case ESP_GAP_BLE_PASSKEY_REQ_EVT: // ESP_IO_CAP_IN
    // The app will receive this evt when the IO has Input capability and the peer device IO has Output capability.
    // See the passkey number on the peer device and send it back.
    ESP_LOGV(TAG, "BLE GAP PASSKEY_REQ");
    // esp_ble_passkey_reply(param->ble_security.ble_req.bd_addr, true, 1234);
    break;

  case ESP_GAP_BLE_SEC_REQ_EVT:
    ESP_LOGV(TAG, "BLE GAP SEC_REQ");
    // Send the positive(true) security response to the peer device to accept the security request.
    // If not accept the security request, should send the security response with negative(false) accept value.
    esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
    break;

  case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:

    ESP_LOGI(TAG, "BLE GAP UPDATE_CONN_PARAMS min_int: %u max_int: %u latency: %u conn_int: %u timeout: %u",
             param->update_conn_params.min_int,
             param->update_conn_params.max_int,
             param->update_conn_params.latency,
             param->update_conn_params.conn_int,
             param->update_conn_params.timeout);
    break;

  default:
    ESP_LOGV(TAG, "BLE GAP EVENT %s", ble_gap_evt_str(event));
    break;
  }
}

esp_err_t
BTKeyboard::start_bt_scan(uint32_t seconds)
{
  esp_err_t ret = ESP_OK;
  if ((ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, (int)(seconds / 1.28), 0)) != ESP_OK)
  {
    ESP_LOGE(TAG, "esp_bt_gap_start_discovery failed: %d", ret);
    return ret;
  }
  return ret;
}

esp_err_t
BTKeyboard::start_ble_scan(uint32_t seconds)
{
  static esp_ble_scan_params_t hid_scan_params = {
      .scan_type = BLE_SCAN_TYPE_ACTIVE,
      .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
      .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
      .scan_interval = 0x50,
      .scan_window = 0x30,
      .scan_duplicate = BLE_SCAN_DUPLICATE_ENABLE,
  };

  esp_err_t ret = ESP_OK;
  if ((ret = esp_ble_gap_set_scan_params(&hid_scan_params)) != ESP_OK)
  {
    ESP_LOGE(TAG, "esp_ble_gap_set_scan_params failed: %d", ret);
    return ret;
  }
  WAIT_BLE_CB();

  if ((ret = esp_ble_gap_start_scanning(seconds)) != ESP_OK)
  {
    ESP_LOGE(TAG, "esp_ble_gap_start_scanning failed: %d", ret);
    return ret;
  }
  return ret;
}

esp_err_t
BTKeyboard::esp_hid_scan(uint32_t seconds, size_t *num_results, esp_hid_scan_result_t **results, bool enable_bt_classic = true)
{
  if (num_bt_scan_results || bt_scan_results || num_ble_scan_results || ble_scan_results)
  {
    ESP_LOGE(TAG, "There are old scan results. Free them first!");
    return ESP_FAIL;
  }

  if (start_ble_scan(seconds) == ESP_OK)
  {
    WAIT_BLE_CB();
  }
  else
  {
    return ESP_FAIL;
  }

  if (enable_bt_classic)
  {
    if (start_bt_scan(seconds) == ESP_OK)
    {
      WAIT_BT_CB();
    }
    else
    {
      return ESP_FAIL;
    }
  }

  *num_results = num_bt_scan_results + num_ble_scan_results;
  *results = bt_scan_results;

  if (num_bt_scan_results)
  {
    while (bt_scan_results->next != NULL)
    {
      bt_scan_results = bt_scan_results->next;
    }
    bt_scan_results->next = ble_scan_results;
  }
  else
  {
    *results = ble_scan_results;
  }

  num_bt_scan_results = 0;
  bt_scan_results = NULL;
  num_ble_scan_results = 0;
  ble_scan_results = NULL;

  return ESP_OK;
}

bool BTKeyboard::devices_scan(int seconds_wait_time)
{
  if (isConnected)
    return true;
  size_t results_len = 0;
  esp_hid_scan_result_t *results = NULL;

  ESP_LOGV(TAG, "SCAN...");

  int numBonded = esp_ble_get_bond_device_num();
  ESP_LOGV(TAG, "Number of bonded devices: %d", numBonded);

  esp_ble_bond_dev_t *dev_list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * numBonded); // bonded device information list

  if ((esp_ble_get_bond_device_list(&numBonded, dev_list)) != ESP_OK)
  { // populate list
    ESP_LOGE(TAG, "esp_ble_get_bond_device_list failed");
    numBonded = 0;
  }

  if (numBonded > 0)
  {
    ESP_LOGI(TAG, "Last bonded device: " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(dev_list[0].bd_addr)); // last one is number 0 (as of ESP v5.0.1)
  }
  // start scan for HID devices

  esp_hid_scan(seconds_wait_time, &results_len, &results);
  ESP_LOGV(TAG, "SCAN: %u results", results_len);

  // check if last bonded device is present

  if (results_len && numBonded > 0)
  {
    ESP_LOGV(TAG, "Checking if bonded started...");
    esp_hid_scan_result_t *r = results;
    esp_hid_scan_result_t connectionRestore;
    esp_hid_scan_result_t *cr = &connectionRestore;
    esp_hid_scan_result_t *rc = NULL;
    bool isLastBonded = false;

    while (r)
    {
      for (int j = 0; j < numBonded; j++)
      {
        for (int i = 0; i < ESP_BD_ADDR_LEN; i++)
        {
          if (r->bda[i] == dev_list[j].bd_addr[i])
            isLastBonded = true;
          else
          {
            isLastBonded = false;
            break;
          }
        }
        if (isLastBonded)
        {
          ESP_LOGI(TAG, "Last bonded device present: " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(dev_list[j].bd_addr));
          break;
        }
        else
        {
          ESP_LOGD(TAG, "Last bonded device NOT present: " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(dev_list[j].bd_addr));
        }
      }

      if (isLastBonded)
      {
        ESP_LOGI(TAG, "Last bonded device present, adding to connect list...");
        *cr = *r;
        if (rc == NULL)
          rc = cr;
        cr = cr->next;
        isLastBonded = false;
      }

      r = r->next;
    }

    esp_hid_scan_result_t *lc = &lastConnected;

    if (rc != NULL)
    {
      while (rc)
      {
        *lc = *rc; // store device for quick-connecting later
        if (esp_hidh_dev_open(rc->bda, rc->transport, rc->ble.addr_type) != NULL)
        {
          ESP_LOGI(TAG, "Connected to device: " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(rc->bda));
        }
        else
        {
          ESP_LOGE(TAG, "esp_hih_dev_open() returned FALSE on device: " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(rc->bda));
        }
        rc = rc->next;
        lc = lc->next;
      }
      esp_hid_scan_results_free(results);
      free(dev_list);
      return true; // WARNING: devices_scan retourning true doesn't mean device connected!! check isConnected for that
    }
  }

  if (results_len)
  {
    esp_hid_scan_result_t *r = results;
    esp_hid_scan_result_t *cr = NULL;
    while (r)
    {
      printf("  %s: " ESP_BD_ADDR_STR ", ", (r->transport == ESP_HID_TRANSPORT_BLE) ? "BLE" : "BT ", ESP_BD_ADDR_HEX(r->bda));
      printf("RSSI: %d, ", r->rssi);
      printf("USAGE: %s, ", esp_hid_usage_str(r->usage));
      if (r->transport == ESP_HID_TRANSPORT_BLE)
      {
        cr = r;
        printf("APPEARANCE: 0x%04x, ", r->ble.appearance);
        printf("ADDR_TYPE: '%s', ", ble_addr_type_str(r->ble.addr_type));
      }
      if (r->transport == ESP_HID_TRANSPORT_BT)
      {
        cr = r;
        btFound = true;
        printf("COD: %s[", esp_hid_cod_major_str(r->bt.cod.major));
        esp_hid_cod_minor_print(r->bt.cod.minor, stdout);
        printf("] srv 0x%03x, ", r->bt.cod.service);
        print_uuid(&r->bt.uuid);
        printf(", ");
      }
      printf("NAME: %s ", r->name ? r->name : "");
      printf("\n");
      r = r->next;
    }
    if (cr)
    {
      // open the last result
      lastConnected = *cr; // store device for quick-connecting later
      if ((esp_hidh_dev_open(cr->bda, cr->transport, cr->ble.addr_type)) != NULL)
      {
        ESP_LOGI(TAG, "Connected to device: " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(cr->bda));
        esp_hid_scan_results_free(results);
        free(dev_list);
        return true; // WARNING: devices_scan retourning true doesn't mean device connected!! check isConnected for that
      }
      else
      {
        ESP_LOGE(TAG, "esp_hidh_dev_open() failed to connect to: " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(cr->bda));
      }
    }
    // free the results
    esp_hid_scan_results_free(results);
    free(dev_list);

    return false;
  }

  esp_hid_scan_results_free(results);
  free(dev_list);

  return false;
}

bool BTKeyboard::devices_scan_ble_daemon(int seconds_wait_time)
{
  size_t results_len = 0;
  esp_hid_scan_result_t *results = NULL;

  ESP_LOGV(TAG, "SCAN DAEMON...");

  int numBonded = esp_ble_get_bond_device_num();
  ESP_LOGV(TAG, "Number of bonded devices: %d", numBonded);

  esp_ble_bond_dev_t *dev_list = (esp_ble_bond_dev_t *)malloc(sizeof(esp_ble_bond_dev_t) * numBonded); // bonded device information list

  if ((esp_ble_get_bond_device_list(&numBonded, dev_list)) != ESP_OK)
  { // populate list
    ESP_LOGE(TAG, "esp_ble_get_bond_device_list failed");
    numBonded = 0;
  }
  // start scan for HID devices

  esp_hid_scan(seconds_wait_time, &results_len, &results, false);
  ESP_LOGV(TAG, "SCAN: %u results", results_len);

  // check if last bonded device is present

  if (results_len && numBonded > 0)
  {
    ESP_LOGV(TAG, "Checking if bonded started...");
    esp_hid_scan_result_t *r = results;
    bool isLastBonded = false;

    while (r)
    {
      for (int j = 0; j < numBonded; j++)
      {
        for (int i = 0; i < ESP_BD_ADDR_LEN; i++)
        {
          if (r->bda[i] == dev_list[j].bd_addr[i])
            isLastBonded = true;
          else
          {
            isLastBonded = false;
            break;
          }
        }
        if (isLastBonded)
        {
          if (esp_hidh_dev_open(r->bda, r->transport, r->ble.addr_type) != NULL)
          {
            ESP_LOGI(TAG, "Connected to device: " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(r->bda));
          }
          else
          {
            ESP_LOGE(TAG, "esp_hih_dev_open() returned FALSE on device: " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(r->bda));
          }
          isLastBonded = false;
          break;
        }
      }
      r = r->next;
    }
    esp_hid_scan_results_free(results);
    free(dev_list);
    return true; // WARNING: devices_scan retourning true doesn't mean device connected!! check isConnected for that
  }

  esp_hid_scan_results_free(results);
  free(dev_list);
  return false;
}

void BTKeyboard::hidh_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
  esp_hidh_event_t event = (esp_hidh_event_t)id;
  esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;
  std::pair<esp_hidh_dev_t *, uint16_t> key_pair;

  switch (event)
  {
  case ESP_HIDH_OPEN_EVENT:
    // { // Code for ESP-IDF 4.3.1
    //   const uint8_t *bda = esp_hidh_dev_bda_get(param->open.dev);
    //   ESP_LOGV(TAG, ESP_BD_ADDR_STR " OPEN: %s", ESP_BD_ADDR_HEX(bda), esp_hidh_dev_name_get(param->open.dev));
    //   esp_hidh_dev_dump(param->open.dev, stdout);
    //   break;
    // }
    { // Code for ESP-IDF 4.4
      if (param->open.status == ESP_OK)
      {
        const esp_hid_transport_t tra = esp_hidh_dev_transport_get(param->open.dev);

        if (tra == ESP_HID_TRANSPORT_BLE)
        {
          esp_ble_conn_update_params_t conn_params;
          esp_gap_conn_params_t current_params;
          esp_err_t ret = esp_ble_get_current_conn_params(const_cast<uint8_t *>(esp_hidh_dev_bda_get(param->open.dev)), &current_params);
          if ((ret == ESP_OK) && (current_params.timeout < 120))
          {
            memcpy(conn_params.bda, esp_hidh_dev_bda_get(param->open.dev), sizeof(esp_bd_addr_t));
            conn_params.latency = current_params.latency;
            conn_params.max_int = current_params.interval; // We preserve the device-negotiated intervals
            conn_params.min_int = 6;
            conn_params.timeout = 400; // timeout = 400*10ms = 4000ms this is a fix because some devices like my Redragon Draconic K530 disconnected a lot
            // start sent the update connection parameters to the peer device.
            esp_ble_gap_update_conn_params(&conn_params);
          }
          else
          {
            ESP_LOGW(TAG, "Couldn't get the BLE connection parameters from the device or they don't need to be updated.");
          }
        }

        size_t num_maps;
        esp_hid_raw_report_map_t *report_maps;
        if (esp_hidh_dev_report_maps_get(param->open.dev, &num_maps, &report_maps))
        {
          ESP_LOGE(TAG, " Failed getting report maps for the device.");
        }

        ESP_LOG_BUFFER_HEX_LEVEL("RAW REPORT MAP: ", report_maps[0].data, report_maps[0].len, ESP_LOG_DEBUG);

        const uint8_t *bda = esp_hidh_dev_bda_get(param->open.dev);

        ESP_LOGV(TAG, ESP_BD_ADDR_STR " OPEN: %s", ESP_BD_ADDR_HEX(bda), esp_hidh_dev_name_get(param->open.dev));
        esp_hidh_dev_dump(param->open.dev, stdout);
        if (hid_report_parse_multimedia_keys(report_maps[0].data, report_maps[0].len, param->open.dev))
        {
          ESP_LOGE(TAG, " Failed parsing report maps for the device.");
        }
        isConnected = true;
        // The following copies the bda of the connected device and it's transport type
        // It is a guard to retrieve a BT Classic device's address that has previously connected
        // as it could connect outside the SCAN routines at boot
        lastConnected.transport = tra;
        std::copy(bda, bda + ESP_BD_ADDR_LEN, lastConnected.bda);
      }
      else
      {
        ESP_LOGE(TAG, " OPEN failed!");
        isConnected = false;
      }
      break;
    }
  case ESP_HIDH_BATTERY_EVENT:
  {
    const uint8_t *bda = esp_hidh_dev_bda_get(param->battery.dev);
    ESP_LOGV(TAG, ESP_BD_ADDR_STR " BATTERY: %d%%", ESP_BD_ADDR_HEX(bda), param->battery.level);
    bt_keyboard->set_battery_level(param->battery.level);
    break;
  }
  case ESP_HIDH_INPUT_EVENT:
  {
    ESP_LOGV(TAG, "INPUT: %8s, MAP: %2u, ID: %3u, Len: %d, Data:",
             esp_hid_usage_str(param->input.usage),
             param->input.map_index,
             param->input.report_id,
             param->input.length);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, param->input.data, param->input.length, ESP_LOG_DEBUG);

    // MOUSE INPUT
    if (param->input.usage == ESP_HID_USAGE_MOUSE) // Filter report usages to kb only type
    {
      key_pair = std::make_pair(param->input.dev, param->input.report_id); // construct the key for the map (multi-device support)
      bt_keyboard->mouse_handle(param->input.data, &key_pair);
    }

    // KEYBOARD INPUT
    else if ((param->input.usage == ESP_HID_USAGE_KEYBOARD) && (param->input.length > 1)) // Filter report usages to kb only type
    {
      bt_keyboard->push_key(param->input.data, param->input.length);
    }

    // MULTIMEDIA KEYS INPUT (CCONTROL)
    else if (param->input.usage == ESP_HID_USAGE_CCONTROL)
    {
      key_pair = std::make_pair(param->input.dev, param->input.report_id); // construct the key for the map (multi-device support)

      if (!multimedia_reports[key_pair].contains_array)
      {
        multimedia_keys.clear();
        uint8_t mask = 1;
        int bit_count = 0;
        uint8_t *p_data = param->input.data;

        for (int k = 0; k < param->input.length; k++) // for every byte on the non-array report
        {
          for (int i = 0; i < 8; i++)
          {
            if (*p_data & mask)
            {
              multimedia_keys.push_back(multimedia_reports[key_pair].array_usages[bit_count]); // load usage code from parsed map
            }
            mask = mask << 1;
            bit_count++;
          }
          mask = 1;
          p_data++;
        }

        bt_keyboard->push_key_CCONTROL(multimedia_keys.data(), multimedia_keys.size());
      }
      else // report contains array data (pure usage codes)
      {
        multimedia_keys.clear();
        uint32_t key_temp;
        uint8_t bytes_to_copy = (multimedia_reports[key_pair].input_len / multimedia_reports[key_pair].report_count) / 8;
        uint8_t *p_data = param->input.data;

        ESP_LOGD(TAG, "Amount of array bytes per key: %d", bytes_to_copy);

        if (bytes_to_copy <= 4)
        {
          for (int i = multimedia_reports[key_pair].report_count; i > 0; i--)
          {
            memcpy(&key_temp, p_data, bytes_to_copy);
            multimedia_keys.push_back(key_temp);
            p_data = p_data + bytes_to_copy;
          }
        }

        bt_keyboard->push_key_CCONTROL(multimedia_keys.data(), multimedia_keys.size());
      }
    }

    break;
  }
  case ESP_HIDH_FEATURE_EVENT:
  {
    const uint8_t *bda = esp_hidh_dev_bda_get(param->feature.dev);
    ESP_LOGV(TAG, ESP_BD_ADDR_STR " FEATURE: %8s, MAP: %2u, ID: %3u, Len: %d",
             ESP_BD_ADDR_HEX(bda),
             esp_hid_usage_str(param->feature.usage),
             param->feature.map_index,
             param->feature.report_id,
             param->feature.length);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, param->feature.data, param->feature.length, ESP_LOG_DEBUG);
    break;
  }
  case ESP_HIDH_CLOSE_EVENT:
  {
    if (param->close.dev != NULL)
    {
      const uint8_t *bda = esp_hidh_dev_bda_get(param->close.dev);
      ESP_LOGI(TAG, ESP_BD_ADDR_STR " CLOSE: %s REASON: %i", ESP_BD_ADDR_HEX(bda), esp_hidh_dev_name_get(param->close.dev), param->close.reason);
    }
    isConnected = false;
    break;
  }
  default:
    ESP_LOGV(TAG, "EVENT: %d", event);
    break;
  }
}

void BTKeyboard::mouse_handle(uint8_t *report_data, std::pair<esp_hidh_dev_t *, uint16_t> *key_pair)
{
  Mouse_Control mouse;

  mouse.mouse_x = getBits(report_data, mouse_reports[*key_pair].mouse_x_bit_index, mouse_reports[*key_pair].mouse_x_bit_lenght);
  mouse.mouse_y = getBits(report_data, mouse_reports[*key_pair].mouse_y_bit_index, mouse_reports[*key_pair].mouse_y_bit_lenght);
  mouse.mouse_w = (int8_t)getBits(report_data, mouse_reports[*key_pair].mouse_w_bit_index, mouse_reports[*key_pair].mouse_w_bit_lenght);
  mouse.mouse_buttons = (uint8_t)getBits(report_data, mouse_reports[*key_pair].mouse_buttons_bit_index, mouse_reports[*key_pair].mouse_buttons_amount); // only a single byte for mouse buttons is supported (3 mouse buttons will be used later only)

  ESP_LOGD(TAG, "Mouse passed to MAIN: B: %u X: %d Y: %d W: %d", mouse.mouse_buttons, mouse.mouse_x, mouse.mouse_y, mouse.mouse_w);
  xQueueSend(event_queue_MOUSE, &mouse, 0);
}

void BTKeyboard::push_key(uint8_t *keys, uint8_t size)
{
  uint8_t offset = 2; // default to BOOT mode report, second byte is always 0

  if (keys[1]) // some keyboards use REPORT mode and the second byte of the report is the first one
  {
    offset = 1;
  }

  KeyInfo inf;
  inf.modifier = (KeyModifier)keys[0];

  uint8_t max = (size > MAX_KEY_COUNT + offset) ? MAX_KEY_COUNT : size - offset;
  // inf.keys[0] = inf.keys[1] = inf.keys[2] = 0; // No need to do this?
  for (int i = 0; i < max; i++)
  {
    inf.keys[i] = keys[i + offset];
  }
  if (max < MAX_KEY_COUNT) // End flag in case our keyboard reports less keys than our max count
    inf.keys[max] = 0;
  ESP_LOG_BUFFER_HEX_LEVEL("KEYS PASSED TO MAIN: ", inf.keys, max, ESP_LOG_DEBUG);
  ESP_LOGD("bt_keyboard", "MODIFIER: 0x%x", inf.modifier);
  xQueueSend(event_queue, &inf, 0);
}

void BTKeyboard::push_key_CCONTROL(uint16_t *keys, uint8_t size)
{
  ESP_LOGD(TAG, "First Key: %x", *keys);

  KeyInfo_CCONTROL inf;

  uint8_t max = (size > MAX_KEY_COUNT) ? MAX_KEY_COUNT : size;

  for (int i = 0; i < max; i++)
  {
    inf.keys[i] = keys[i];
  }
  if (max < MAX_KEY_COUNT) // End flag in case our keyboard reports less keys than our max count
    inf.keys[max] = 0;
  ESP_LOG_BUFFER_HEX_LEVEL("MULTIMEDIA CCONTROL KEYS PASSED TO MAIN (Hex Dump): ", inf.keys, max * 2, ESP_LOG_DEBUG);
  xQueueSend(event_queue_CCONTROL, &inf, 0);
}

char BTKeyboard::wait_for_ascii_char(bool forever)
{
  KeyInfo inf;

  while (true)
  {
    if (!wait_for_low_event(inf, (last_ch == 0) ? (forever ? portMAX_DELAY : 0) : repeat_period))
    {
      repeat_period = pdMS_TO_TICKS(120);
      return last_ch;
    }

    int k = -1;
    for (int i = 0; i < MAX_KEY_COUNT; i++)
    {
      if ((k < 0) && key_avail[i])
        k = i;
      key_avail[i] = inf.keys[i] == 0;
    }

    if (k < 0)
    {
      continue;
    }

    char ch = inf.keys[k];

    if (ch >= 4)
    {
      if ((uint8_t)inf.modifier & CTRL_MASK)
      {
        if (ch < (3 + 26))
        {
          repeat_period = pdMS_TO_TICKS(500);
          return last_ch = (ch - 3);
        }
      }
      else if (ch <= 0x52)
      {
        // ESP_LOGI(TAG, "Scan code: %d", ch);
        if (ch == KEY_CAPS_LOCK)
          caps_lock = !caps_lock;
        if ((uint8_t)inf.modifier & SHIFT_MASK)
        {
          if (caps_lock)
          {
            repeat_period = pdMS_TO_TICKS(500);
            return last_ch = shift_trans_dict[(ch - 4) << 1];
          }
          else
          {
            repeat_period = pdMS_TO_TICKS(500);
            return last_ch = shift_trans_dict[((ch - 4) << 1) + 1];
          }
        }
        else
        {
          if (caps_lock)
          {
            repeat_period = pdMS_TO_TICKS(500);
            return last_ch = shift_trans_dict[((ch - 4) << 1) + 1];
          }
          else
          {
            repeat_period = pdMS_TO_TICKS(500);
            return last_ch = shift_trans_dict[(ch - 4) << 1];
          }
        }
      }
    }

    last_ch = 0;
  }
}

void BTKeyboard::quick_reconnect(void) // tries to connect to the latest bonded device, without a scan
{
  while (!BTKeyboard::isConnected)
  {
    ESP_LOGI(TAG, "Trying to reconnect To: " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(lastConnected.bda));
    ESP_LOGI(TAG, "TRANSPORT:  %s", (lastConnected.transport == ESP_HID_TRANSPORT_BLE) ? "BLE" : "BT ");
    esp_hidh_dev_open(lastConnected.bda, lastConnected.transport, lastConnected.ble.addr_type);
    ESP_LOGI(TAG, "Tried to reconnect.");
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

esp_err_t BTKeyboard::hid_report_parse_multimedia_keys(const uint8_t *hid_rm, size_t hid_rm_len, esp_hidh_dev_t *device)
{
  size_t index = 0;
  int res;

  while (index < hid_rm_len)
  {
    hid_report_cmd_t *cmd;
    res = parse_cmd(hid_rm, hid_rm_len, index, &cmd);
    if (res < 0)
    {
      ESP_LOGE("BT_Keyboard hid_report_parse_multimedia_keys:", " Failed parsing the descriptor at index: %u", index);
      return ESP_FAIL;
    }
    index += res;

    res = handle_cmd(cmd, device);
    free(cmd);
    if (res != 0)
    {
      return ESP_FAIL;
    }
  }

  return ESP_OK;
}

int BTKeyboard::parse_cmd(const uint8_t *data, size_t len, size_t index, hid_report_cmd_t **out)
{
  if (index == len)
  {
    return 0;
  }
  hid_report_cmd_t *cmd = (hid_report_cmd_t *)malloc(sizeof(hid_report_cmd_t));
  if (cmd == NULL)
  {
    return -1;
  }
  const uint8_t *dp = data + index;
  cmd->cmd = *dp & 0xFC;
  cmd->len = *dp & 0x03;
  cmd->value = 0;
  for (int i = 0; i < 4; i++)
  {
    cmd->data[i] = 0;
  }
  if (cmd->len == 3)
  {
    cmd->len = 4;
  }
  if ((len - index - 1) < cmd->len)
  {
    ESP_LOGE("BT_Keyboard parse_cmd:", "not enough bytes! cmd: 0x%02x, len: %u, index: %u", cmd->cmd, cmd->len, index);
    free(cmd);
    return -1;
  }
  memcpy(cmd->data, dp + 1, cmd->len);
  *out = cmd;
  return cmd->len + 1;
}

int BTKeyboard::handle_cmd(hid_report_cmd_t *cmd, esp_hidh_dev_t *device)
{
  switch (s_parse_step)
  {
  case PARSE_WAIT_USAGE_PAGE:
  {
    if (cmd->cmd != HID_RM_USAGE_PAGE)
    {
      ESP_LOGE("BT_Keyboard handle_cmd:", "expected USAGE_PAGE, but got 0x%02x", cmd->cmd);
      return -1;
    }
    s_report_size = 0;
    s_report_count = 0;
    // memset(&s_report_params, 0, sizeof(hid_report_params_t));
    s_report_params = s_report_params_empty;

    s_report_params.usage_page = cmd->value;
    s_parse_step = PARSE_WAIT_USAGE;
    break;
  }
  case PARSE_WAIT_USAGE:
  {
    if (cmd->cmd != HID_RM_USAGE)
    {
      ESP_LOGE("BT_Keyboard handle_cmd:", "expected USAGE, but got 0x%02x", cmd->cmd);
      s_parse_step = PARSE_WAIT_USAGE_PAGE;
      return -1;
    }
    s_report_params.usage = cmd->value;
    s_parse_step = PARSE_WAIT_COLLECTION_APPLICATION;
    break;
  }
  case PARSE_WAIT_COLLECTION_APPLICATION:
  {
    if (cmd->cmd != HID_RM_COLLECTION)
    {
      ESP_LOGE("BT_Keyboard handle_cmd:", "expected COLLECTION, but got 0x%02x", cmd->cmd);
      s_parse_step = PARSE_WAIT_USAGE_PAGE;
      return -1;
    }
    if (cmd->value != 1)
    {
      ESP_LOGE("BT_Keyboard handle_cmd:", "expected APPLICATION, but got 0x%02x", cmd->value);
      s_parse_step = PARSE_WAIT_USAGE_PAGE;
      return -1;
    }
    s_report_params.report_id = 0;
    s_collection_depth = 1;
    s_parse_step = PARSE_WAIT_END_COLLECTION;
    break;
  }
  case PARSE_WAIT_END_COLLECTION:
  {
    if (cmd->cmd == HID_RM_REPORT_ID)
    {
      if (s_report_params.report_id && s_report_params.report_id != cmd->value)
      {
        // report id changed mid collection
        if (s_report_params.input_len & 0x7)
        {
          ESP_LOGE("BT_Keyboard handle_cmd:", "ERROR: INPUT report does not amount to full bytes! %d (%d)", s_report_params.input_len, s_report_params.input_len & 0x7);
        }
        else if (s_report_params.output_len & 0x7)
        {
          ESP_LOGE("BT_Keyboard handle_cmd:", "ERROR: OUTPUT report does not amount to full bytes! %d (%d)", s_report_params.output_len, s_report_params.output_len & 0x7);
        }
        else if (s_report_params.feature_len & 0x7)
        {
          ESP_LOGE("BT_Keyboard handle_cmd:", "ERROR: FEATURE report does not amount to full bytes! %d (%d)", s_report_params.feature_len, s_report_params.feature_len & 0x7);
        }
        else
        {
          // SUCCESS!!!
          int res = handle_report(&s_report_params, device);
          if (res != 0)
          {
            s_parse_step = PARSE_WAIT_USAGE_PAGE;
            return -1;
          }

          s_report_params.input_len = 0;
          s_report_params.output_len = 0;
          s_report_params.feature_len = 0;
          s_report_params.usage = s_report_params.inner_usage;
          s_report_params.usage_page = s_report_params.inner_usage_page;
        }
      }
      s_report_params.report_id = cmd->value;
    }
    else if (cmd->cmd == HID_RM_USAGE_PAGE)
    {
      s_report_params.inner_usage_page = cmd->value;
    }
    else if (cmd->cmd == HID_RM_USAGE)
    {
      s_report_params.inner_usage = cmd->value;
      temp_usages_array.push_back(cmd->value);
      s_usages_count++;
    }
    else if (cmd->cmd == HID_RM_REPORT_SIZE)
    {
      s_report_size = cmd->value;
    }
    else if (cmd->cmd == HID_RM_REPORT_COUNT)
    {
      s_report_count = cmd->value;
    }
    else if (cmd->cmd == HID_RM_LOGICAL_MINIMUM)
    {
      s_report_params.logical_minimum = cmd->value;
    }
    else if (cmd->cmd == HID_RM_LOGICAL_MAXIMUM)
    {
      s_report_params.logical_maximum = cmd->value;
    }
    else if (cmd->cmd == HID_RM_USAGE_MAXIMUM)
    {
      s_report_params.usage_maximum = cmd->value;
    }
    else if (cmd->cmd == HID_RM_USAGE_MINIMUM)
    {
      s_report_params.usage_minimum = cmd->value;
    }
    else if (cmd->cmd == HID_RM_INPUT)
    {
      if (s_report_params.usage == HID_USAGE_MOUSE) // process mouse usages
      {
        for (int j = 0; j < temp_usages_array.size(); j++)
        {
          if (temp_usages_array[j] == 0x30) // X
          {
            s_report_params.mouse_x_bit_index = s_report_params.input_len + (j * s_report_size);
            s_report_params.mouse_x_lenght = s_report_size;
          }
          else if (temp_usages_array[j] == 0x31) // Y
          {
            s_report_params.mouse_y_bit_index = s_report_params.input_len + (j * s_report_size);
            s_report_params.mouse_y_lenght = s_report_size;
          }
          else if (temp_usages_array[j] == 0x38) // W (wheel)
          {
            s_report_params.mouse_w_bit_index = s_report_params.input_len + (j * s_report_size);
            s_report_params.mouse_w_lenght = s_report_size;
          }
        }
        temp_usages_array.clear();

        if (!(cmd->value & 0x1) && (s_report_params.inner_usage_page == 0x09)) // data (no const) & button usage page check
        {
          s_report_params.mouse_buttons_bit_index = s_report_params.input_len;
          s_report_params.mouse_buttons_amount = s_report_count;
        }
      }
      else // process keyboard usages
      {
        int i = (s_report_size * s_report_count) - s_usages_count; // usages vector: fill with 0's when no usage is added
        for (int k = 0; k < i; k++)
        {
          temp_usages_array.push_back(0);
        }
      }

      s_usages_count = 0;

      if (!(cmd->value & 0x2)) // array check
      {
        s_report_params.contains_array = true;
      }

      s_report_params.input_len += (s_report_size * s_report_count); // input lenght is always increased (LAST)
    }
    else if (cmd->cmd == HID_RM_OUTPUT)
    {
      s_report_params.output_len += (s_report_size * s_report_count);
    }
    else if (cmd->cmd == HID_RM_FEATURE)
    {
      s_report_params.feature_len += (s_report_size * s_report_count);
    }
    else if (cmd->cmd == HID_RM_COLLECTION)
    {
      s_collection_depth += 1;
    }
    else if (cmd->cmd == HID_RM_END_COLLECTION)
    {
      s_collection_depth -= 1;
      if (s_collection_depth == 0)
      {
        if (s_report_params.input_len & 0x7)
        {
          ESP_LOGE("BT_Keyboard handle_cmd:", "ERROR: INPUT report does not amount to full bytes! %d (%d)", s_report_params.input_len, s_report_params.input_len & 0x7);
        }
        else if (s_report_params.output_len & 0x7)
        {
          ESP_LOGE("BT_Keyboard handle_cmd:", "ERROR: OUTPUT report does not amount to full bytes! %d (%d)", s_report_params.output_len, s_report_params.output_len & 0x7);
        }
        else if (s_report_params.feature_len & 0x7)
        {
          ESP_LOGE("BT_Keyboard handle_cmd:", "ERROR: FEATURE report does not amount to full bytes! %d (%d)", s_report_params.feature_len, s_report_params.feature_len & 0x7);
        }
        else
        {
          // SUCCESS!!!
          int res = handle_report(&s_report_params, device);
          if (res != 0)
          {
            s_parse_step = PARSE_WAIT_USAGE_PAGE;
            return -1;
          }
        }
        s_parse_step = PARSE_WAIT_USAGE_PAGE;
      }
    }

    break;
  }
  default:
    s_parse_step = PARSE_WAIT_USAGE_PAGE;
    break;
  }
  return 0;
}

int BTKeyboard::handle_report(hid_report_params_t *report, esp_hidh_dev_t *device)
{
  std::pair<esp_hidh_dev_t *, uint16_t> key = std::make_pair(device, report->report_id); // device-report ID unique identifier.

  if (report->usage_page == HID_USAGE_PAGE_CONSUMER_DEVICE && report->usage == HID_USAGE_CONSUMER_CONTROL)
  {
    multimedia_reports[key].array_usages = temp_usages_array;
    multimedia_reports[key].contains_array = report->contains_array;
    multimedia_reports[key].input_len = report->input_len;
    multimedia_reports[key].logical_maximum = report->logical_maximum;
    multimedia_reports[key].logical_minimum = report->logical_minimum;
    multimedia_reports[key].report_id = report->report_id;
    multimedia_reports[key].usage_maximum = report->usage_maximum;
    multimedia_reports[key].usage_minimum = report->usage_minimum;
    multimedia_reports[key].report_count = BTKeyboard::s_report_count;

    ESP_LOGD("bt_Keyboard handle_report: ", "Amount of CCONTROL reports: %u", multimedia_reports.size());
  }

  if (report->usage_page == HID_USAGE_PAGE_GENERIC_DESKTOP && report->usage == HID_USAGE_MOUSE)
  {
    mouse_reports[key].report_id = report->report_id;
    mouse_reports[key].input_len = report->input_len;
    mouse_reports[key].mouse_x_bit_index = report->mouse_x_bit_index;
    mouse_reports[key].mouse_y_bit_index = report->mouse_y_bit_index;
    mouse_reports[key].mouse_w_bit_index = report->mouse_w_bit_index;
    mouse_reports[key].mouse_x_bit_lenght = report->mouse_x_lenght;
    mouse_reports[key].mouse_y_bit_lenght = report->mouse_y_lenght;
    mouse_reports[key].mouse_w_bit_lenght = report->mouse_w_lenght;
    mouse_reports[key].mouse_buttons_bit_index = report->mouse_buttons_bit_index;
    mouse_reports[key].mouse_buttons_amount = report->mouse_buttons_amount;

    ESP_LOGD("bt_Keyboard handle_report: ", "Amount of MOUSE reports: %u X: %u Y: %u W: %u Xl: %u Yl: %u Wl: %u B: %u Bl: %u", mouse_reports.size(),
             mouse_reports[key].mouse_x_bit_index,
             mouse_reports[key].mouse_y_bit_index,
             mouse_reports[key].mouse_w_bit_index,
             mouse_reports[key].mouse_x_bit_lenght,
             mouse_reports[key].mouse_y_bit_lenght,
             mouse_reports[key].mouse_w_bit_lenght,
             mouse_reports[key].mouse_buttons_bit_index,
             mouse_reports[key].mouse_buttons_amount);
  }

  temp_usages_array.clear();
  s_usages_count = 0;
  return 0;
}

int16_t BTKeyboard::getBits(const void *Data, uint16_t StartBit, uint16_t NumBits)
{

  // get a pointer to the starting byte...
  const uint8_t *pData = &(static_cast<const uint8_t *>(Data)[StartBit / 8]);

  uint16_t data;
  uint16_t signBit;
  uint16_t mask;
  uint16_t extendMask;
  uint16_t startBit;
  uint16_t lastByte;

  startBit = StartBit & 7;
  lastByte = (NumBits - 1) / 8;

  /* Pick up the data bytes backwards */
  data = 0;
  do
  {
    data <<= 8;
    data |= *(pData + lastByte);
  } while (lastByte--);

  /* Shift to the right to byte align the least significant bit */
  if (startBit > 0)
  {
    data >>= startBit;
  }

  /* Done if 16 bits long */
  if (NumBits < 16)
  {

    /* Mask off the other bits */
    mask = 1 << NumBits;
    mask--;
    data &= mask;

    /* Sign extend the report item */
    signBit = 1;
    if (NumBits > 1)
      signBit <<= (NumBits - 1);
    extendMask = (signBit << 1) - 1;
    if ((data & signBit) == 0)
      data &= extendMask;
    else
      data |= ~extendMask;
  }

  return data;
}