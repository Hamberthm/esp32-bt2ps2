// This module contains code from the PS/2 to Serial mouse project by Necroware
// Adapted and modified by Hambert - HamCode - 2024

#include "..\include\serial_mouse.h"
#include "..\include\esp32-ps2dev.h"
#define NOP() asm volatile("nop")

static bool threeButtons = false;

void reset_watchdog(void *args);
void serial_daemon(void *args);

bool serialMouse::resetReceived = false;
TaskHandle_t serialMouse::handle_reset_watchdog = NULL;
TaskHandle_t serialMouse::handle_serial_daemon = NULL;
int serialMouse::RS232_RTS;
int serialMouse::RS232_RX;

unsigned long IRAM_ATTR serialMouse::micros() // define inexistent Arduino IDE functions
{
    return (unsigned long)(esp_timer_get_time());
}

void IRAM_ATTR serialMouse::delayMicroseconds(uint32_t us)
{
    uint32_t m = micros();
    if (us)
    {
        uint32_t e = (m + us);
        if (m > e)
        { // overflow
            while (micros() > e)
            {
                NOP();
            }
        }
        while (micros() < e)
        {
            NOP();
        }
    }
}

template <class T>
const T &constrain(const T &x, const T &a, const T &b)
{
    if (x < a)
    {
        return a;
    }
    else if (b < x)
    {
        return b;
    }
    else
        return x;
}

// Delay between the signals to match 1200 baud
static const auto usDelay = 1000000 / 1200;

void serialMouse::sendSerialBit(int data)
{
    gpio_set_level((gpio_num_t)RS232_RX, data);
    delayMicroseconds(usDelay);
}

void serialMouse::sendSerialByte(uint8_t data)
{
    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    taskENTER_CRITICAL(&mux);
    // Start bit
    sendSerialBit(0);

    // Data bits
    for (int i = 0; i < 7; i++)
    {
        sendSerialBit((data >> i) & 0x01);
    }

    // Stop bit
    sendSerialBit(1);

    // 7+1 bits is normal mouse protocol, but some serial controllers
    // expect 8+1 bits format. We send additional stop bit to stay
    // compatible to that kind of controllers.
    sendSerialBit(1);
    taskEXIT_CRITICAL(&mux);
}

void serialMouse::sendToSerial(const serialMouse::Data &data)
{
    auto dx = constrain(data.xMovement, -127, 127);
    auto dy = constrain(-data.yMovement, -127, 127);
    uint8_t lb = (data.buttons & 0b001) ? 0x20 : 0;
    uint8_t rb = (data.buttons & 0b010) ? 0x10 : 0;
    sendSerialByte(0x40 | lb | rb | ((dy >> 4) & 0xC) | ((dx >> 6) & 0x3));
    sendSerialByte(dx & 0x3F);
    sendSerialByte(dy & 0x3F);
    if (threeButtons)
    {
        uint8_t mb = (data.buttons & 0b100) ? 0x20 : 0;
        sendSerialByte(mb);
    }
}

void serialMouse::initSerialPort()
{
    gpio_set_level((gpio_num_t)RS232_RX, 1);
    delayMicroseconds(10000);
    sendSerialByte('M');
    if (threeButtons)
    {
        sendSerialByte('3');
        // ESP_LOGI(TAG, "Init 3-buttons mode");
    }
    delayMicroseconds(10000);
}

void serialMouse::setup(int rtsPin, int rxPin)
{
    RS232_RTS = rtsPin;
    RS232_RX = rxPin;

    if (handle_reset_watchdog == NULL)
    {
        xTaskCreatePinnedToCore(reset_watchdog, "serial reset watchdog", 4096, NULL, 10, &handle_reset_watchdog, esp32_ps2dev::DEFAULT_TASK_CORE_MOUSE);
        xTaskCreatePinnedToCore(serial_daemon, "Serial daemon", 4096, this, 9, &handle_serial_daemon, esp32_ps2dev::DEFAULT_TASK_CORE_MOUSE);
    }

    initSerialPort();

    ESP_LOGI(TAG, "Setup done");
}

void serialMouse::serialMove(uint8_t buttons, int16_t mouseX, int16_t mouseY)
{
    report_data.buttons = report_data.buttons | buttons;
    report_data.xMovement += mouseX;
    report_data.yMovement += mouseY;

    xTaskNotifyGive(handle_serial_daemon);
}

static void IRAM_ATTR rtsInterrupt(void *args)
{

    BaseType_t xHigherPriorityTaskWoken;
    xHigherPriorityTaskWoken = pdFALSE;

    vTaskNotifyGiveFromISR(serialMouse::handle_reset_watchdog, &xHigherPriorityTaskWoken);
}

void reset_watchdog(void *args)
{
    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)serialMouse::RS232_RTS, rtsInterrupt, (void *)(gpio_num_t)serialMouse::RS232_RTS);

    serialMouse::resetReceived = false;

    const TickType_t xBlockTime = portMAX_DELAY;
    uint32_t ulNotifiedValue;

    while (true)
    {
        ulNotifiedValue = ulTaskNotifyTake(pdTRUE, xBlockTime);
        if (ulNotifiedValue > 0)
        {
            serialMouse::resetReceived = true;
            serialMouse::initSerialPort();
            serialMouse::resetReceived = false;
            ESP_LOGI("Serial mouse reset_watchdog: ", "Mouse reset event has been processed.");
        }
    }
}

void serial_daemon(void *arg)
{
    serialMouse *mouseSer = (serialMouse *)arg;
    while (true)
    {
        xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);
        mouseSer->report_buffer = mouseSer->report_data;
        mouseSer->report_data.buttons = 0;
        mouseSer->report_data.xMovement = 0;
        mouseSer->report_data.yMovement = 0;
        if (!serialMouse::resetReceived)
            mouseSer->sendToSerial(mouseSer->report_buffer);
    }
    vTaskDelete(NULL);
}