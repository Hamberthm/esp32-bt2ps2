#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"

class serialMouse
{
public:
    struct Data
    {
        uint8_t buttons;
        int xMovement;
        int yMovement;
    } report_data, report_buffer;

    void setup(int rtsPin, int rxPin);
    void serialMove(uint8_t buttons, int16_t mouseX, int16_t mouseY);
    static void initSerialPort();
    static TaskHandle_t handle_reset_watchdog;
    static TaskHandle_t handle_serial_daemon;
    static bool resetReceived; // Host system issued a reset command via RTS pin
    static int RS232_RTS;
    static int RS232_RX;
    static void sendToSerial(const serialMouse::Data &data);

private:
    static constexpr char const *TAG = "serial_mouse";
    static unsigned long IRAM_ATTR micros();
    static void IRAM_ATTR delayMicroseconds(uint32_t us);
    static void sendSerialBit(int data);
    static void sendSerialByte(uint8_t data);
};