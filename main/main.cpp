#include "..\include\globals.hpp"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "..\include\bt_keyboard.hpp"
#include <iostream>
#include <cmath>

// #include <Arduino.h>              //
#include "..\include\esp32-ps2dev.h" // Emulate a PS/2 device

static constexpr char const *TAG = "BTKeyboard";

// PS/2 emulation variables
const int CLK_PIN = 13;
const int DATA_PIN = 12;
esp32_ps2dev::PS2Keyboard keyboard(CLK_PIN, DATA_PIN);

// BTKeyboard section
BTKeyboard bt_keyboard;

int NumDigits(int x)
{
    x = abs(x);
    return (x < 10 ? 1 : (x < 100 ? 2 : (x < 1000 ? 3 : (x < 10000 ? 4 : (x < 100000 ? 5 : (x < 1000000 ? 6 : (x < 10000000 ? 7 : (x < 100000000 ? 8 : (x < 1000000000 ? 9 : 10)))))))));
}

void pairing_handler(uint32_t pid)
{
    int x = (int)pid;
    std::cout << "Please enter the following pairing code, "
              << std::endl
              << "followed with ENTER on your keyboard: "
              << pid
              << std::endl;

    for (int i = 0; i < 10; i++)
    {
        gpio_set_level(GPIO_NUM_2, 1);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        gpio_set_level(GPIO_NUM_2, 0);
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }

    int dig = NumDigits(x);

    for (int i = 1; i <= dig; i++)
    {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        ESP_LOGI(TAG, "Codigo es %d ", pid);
        int flash = ((int)((pid / pow(10, (dig - i)))) % 10);
        ESP_LOGI(TAG, "Flashing %d times", flash);
        for (int n = 0; n < flash; n++)
        {
            gpio_set_level(GPIO_NUM_2, 1);
            vTaskDelay(200 / portTICK_PERIOD_MS);
            gpio_set_level(GPIO_NUM_2, 0);
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }

        if (flash < 1)
        {
            gpio_set_level(GPIO_NUM_2, 1);
            vTaskDelay(1500 / portTICK_PERIOD_MS);
            gpio_set_level(GPIO_NUM_2, 0);
            vTaskDelay(200 / portTICK_PERIOD_MS);
        }
    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    for (int i = 0; i < 10; i++)
    {

        gpio_set_level(GPIO_NUM_2, 1);
        vTaskDelay(50 / portTICK_PERIOD_MS);
        gpio_set_level(GPIO_NUM_2, 0);
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

extern "C"
{

    void app_main(void)
    {
        //PIN CONFIGURATION (NEEDED IN ESP IDF, NOT IN ARDUINO)
        gpio_config_t io_conf;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << DATA_PIN);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);
        io_conf.pin_bit_mask = (1ULL << CLK_PIN);
        gpio_config(&io_conf);

        // init PS/2 emulation first
        // Serial.begin(115200); //only compatible in Arduino IDE
        gpio_reset_pin(GPIO_NUM_2);                       // using built-in LED for notifications
        gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT); // Set the GPIO as a push/pull output
        gpio_set_level(GPIO_NUM_2, 1);
        keyboard.begin();
        gpio_set_level(GPIO_NUM_2, 0);

        // init BTKeyboard
        esp_err_t ret;

        // To test the Pairing code entry, uncomment the following line as pairing info is
        // kept in the nvs. Pairing will then be required on every boot.
        // ESP_ERROR_CHECK(nvs_flash_erase());

        ret = nvs_flash_init();
        if ((ret == ESP_ERR_NVS_NO_FREE_PAGES) || (ret == ESP_ERR_NVS_NEW_VERSION_FOUND))
        {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);

        if (bt_keyboard.setup(pairing_handler))
        { // Must be called once
            while (!bt_keyboard.devices_scan())
            {
                if (BTKeyboard::btFound)
                {
                    BTKeyboard::btFound = false;
                    for (int i = 0; i < 60; i++)
                    {
                        if (BTKeyboard::isConnected)
                            break;
                        ESP_LOGI(TAG, "Waiting for BT manual code entry...");
                        vTaskDelay(1000 / portTICK_PERIOD_MS);
                    }
                }
                else
                {
                    vTaskDelay(1000 / portTICK_PERIOD_MS);
                    ESP_LOGI(TAG, "Rescan...");
                }
            }                              // Required to discover new keyboards and for pairing
                                           // Default duration is 5 seconds
            gpio_set_level(GPIO_NUM_2, 1); // success, device found
        }

        // time variables, don't adjust unless you know what you're doing
        uint8_t typematicRate = 20;    // characters per second in Typematic mode
        uint16_t typematicDelay = 500; // ms to become Typematic

        // fixed stuff
        uint8_t cycle = 1000 / typematicRate;            // keywait timeout in ms. Important so we can check connection and do Typematic
        TickType_t repeat_period = pdMS_TO_TICKS(cycle); // keywait timeout in ticks. Important so we can check connection and do Typematic
        BTKeyboard::KeyInfo inf;                         // freshly received
        BTKeyboard::KeyInfo infBuf;                      // currently pressed
        bool found = false;                              // just an innocent flasg I mean flag
        int typematicLeft = typematicDelay;              // timekeeping

        while (true)
        {
            if (bt_keyboard.wait_for_low_event(inf, repeat_period))
            {
                // Handle modifier keys
                if (inf.modifier != infBuf.modifier)
                {

                    // MODIFIER SECTION

                    // Are you a communist?
                    if (((uint8_t)inf.modifier & 0x0f) != ((uint8_t)infBuf.modifier & 0x0f))
                    {

                        // LSHIFT
                        if (((uint8_t)inf.modifier & 0x02) != ((uint8_t)infBuf.modifier & 0x02))
                        {
                            if ((uint8_t)inf.modifier & 0x02)
                            {
                                ESP_LOGI(TAG, "Down key: LSHIFT");
                                keyboard.keydown(esp32_ps2dev::scancodes::Key::K_LSHIFT);
                            }
                            else
                            {
                                ESP_LOGI(TAG, "Up key: LSHIFT");
                                keyboard.keyup(esp32_ps2dev::scancodes::Key::K_LSHIFT);
                            }
                        }

                        // LCTRL
                        if (((uint8_t)inf.modifier & 0x01) != ((uint8_t)infBuf.modifier & 0x01))
                        {
                            if ((uint8_t)inf.modifier & 0x01)
                            {
                                ESP_LOGI(TAG, "Down key: LCTRL");
                                keyboard.keydown(esp32_ps2dev::scancodes::Key::K_LCTRL);
                            }
                            else
                            {
                                ESP_LOGI(TAG, "Up key: LCTRL");
                                keyboard.keyup(esp32_ps2dev::scancodes::Key::K_LCTRL);
                            }
                        }

                        // LMETA
                        if (((uint8_t)inf.modifier & 0x08) != ((uint8_t)infBuf.modifier & 0x08))
                        {
                            if ((uint8_t)inf.modifier & 0x08)
                            {
                                ESP_LOGI(TAG, "Down key: LMETA");
                                keyboard.keydown(esp32_ps2dev::scancodes::Key::K_LSUPER);
                            }
                            else
                            {
                                ESP_LOGI(TAG, "Up key: LMETA");
                                keyboard.keyup(esp32_ps2dev::scancodes::Key::K_LSUPER);
                            }
                        }

                        // LALT
                        if (((uint8_t)inf.modifier & 0x04) != ((uint8_t)infBuf.modifier & 0x04))
                        {
                            if ((uint8_t)inf.modifier & 0x04)
                            {
                                ESP_LOGI(TAG, "Down key: LALT");
                                keyboard.keydown(esp32_ps2dev::scancodes::Key::K_LALT);
                            }
                            else
                            {
                                ESP_LOGI(TAG, "Up key: LALT");
                                keyboard.keyup(esp32_ps2dev::scancodes::Key::K_LALT);
                            }
                        }
                    }

                    // Are you a capitalist?
                    if (((uint8_t)inf.modifier & 0xf0) != ((uint8_t)infBuf.modifier & 0xf0))
                    {

                        // RSHIFT
                        if (((uint8_t)inf.modifier & 0x20) != ((uint8_t)infBuf.modifier & 0x20))
                        {
                            if ((uint8_t)inf.modifier & 0x20)
                            {
                                ESP_LOGI(TAG, "Down key: RSHIFT");
                                keyboard.keydown(esp32_ps2dev::scancodes::Key::K_RSHIFT);
                            }
                            else
                            {
                                ESP_LOGI(TAG, "Up key: RSHIFT");
                                keyboard.keyup(esp32_ps2dev::scancodes::Key::K_RSHIFT);
                            }
                        }

                        // RCTRL
                        if (((uint8_t)inf.modifier & 0x10) != ((uint8_t)infBuf.modifier & 0x10))
                        {
                            if ((uint8_t)inf.modifier & 0x10)
                            {
                                ESP_LOGI(TAG, "Down key: RCTRL");
                                keyboard.keydown(esp32_ps2dev::scancodes::Key::K_RCTRL);
                            }
                            else
                            {
                                ESP_LOGI(TAG, "Up key: RCTRL");
                                keyboard.keyup(esp32_ps2dev::scancodes::Key::K_RCTRL);
                            }
                        }

                        // RMETA
                        if (((uint8_t)inf.modifier & 0x80) != ((uint8_t)infBuf.modifier & 0x80))
                        {
                            if ((uint8_t)inf.modifier & 0x80)
                            {
                                ESP_LOGI(TAG, "Down key: RMETA");
                                keyboard.keydown(esp32_ps2dev::scancodes::Key::K_RSUPER);
                            }
                            else
                            {
                                ESP_LOGI(TAG, "Up key: RMETA");
                                keyboard.keyup(esp32_ps2dev::scancodes::Key::K_RSUPER);
                            }
                        }

                        // RALT
                        if (((uint8_t)inf.modifier & 0x40) != ((uint8_t)infBuf.modifier & 0x40))
                        {
                            if ((uint8_t)inf.modifier & 0x40)
                            {
                                ESP_LOGI(TAG, "Down key: RALT");
                                keyboard.keydown(esp32_ps2dev::scancodes::Key::K_RALT);
                            }
                            else
                            {
                                ESP_LOGI(TAG, "Up key: RALT");
                                keyboard.keyup(esp32_ps2dev::scancodes::Key::K_RALT);
                            }
                        }
                    }
                }

                // KEY SECTION (always tested)
                // release the keys that have been just released
                for (int i = 0; i < BTKeyboard::MAX_KEY_COUNT; i++)
                {
                    if (!infBuf.keys[i])
                        break;
                    for (int j = 0; j < BTKeyboard::MAX_KEY_COUNT; j++)
                    {
                        if (infBuf.keys[i] == inf.keys[j])
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        ESP_LOGI(TAG, "Up key: %x", infBuf.keys[i]);
                        keyboard.keyHid_send(infBuf.keys[i], false);
                    }
                    else
                        found = false;
                }

                // press the keys that have been just pressed
                for (int i = 0; i < BTKeyboard::MAX_KEY_COUNT; i++)
                {
                    if (!inf.keys[i])
                        break;
                    for (int j = 0; j < BTKeyboard::MAX_KEY_COUNT; j++)
                    {
                        if (inf.keys[i] == infBuf.keys[j])
                        {
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        ESP_LOGI(TAG, "Down key: %x", inf.keys[i]);
                        keyboard.keyHid_send(inf.keys[i], true);
                    }
                    else
                        found = false;
                }

                infBuf = inf;                   // Now all the keys are handled, we save the state
                typematicLeft = typematicDelay; // Typematic timer reset
            }

            else
            {
                if (infBuf.keys[0])
                { // If any key held down, do the Typematic dance
                    typematicLeft = typematicLeft - cycle;
                    if (typematicLeft <= 0)
                    {
                        for (int i = 1; i < BTKeyboard::MAX_KEY_COUNT; i++)
                        {
                            if (infBuf.keys[i] == 0)
                            {
                                if (infBuf.keys[i - 1] != 0x39)
                                { // Please don't repeat caps, it's ugly
                                    ESP_LOGI(TAG, "Down key: %x", infBuf.keys[i - 1]);
                                    keyboard.keyHid_send(infBuf.keys[i - 1], true); // Resend the last key
                                }
                                break;
                            }
                        }
                    }
                }

                while (!BTKeyboard::isConnected)
                {                                  // check connection
                    gpio_set_level(GPIO_NUM_2, 0); // disconnected
                    bt_keyboard.quick_reconnect();
                    vTaskDelay(250 / portTICK_PERIOD_MS);
                }
                gpio_set_level(GPIO_NUM_2, 1);
            }
        }
    }
}
