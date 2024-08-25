24/8/24: Mouse and Multimedia Key support is out for v0.6! Check notes on release

Tested Bluetooth Keyboards and Mice (Please add your own!): https://1drv.ms/x/s!AlKre4_rNmpJiYpl1v4KcbK1Pm77zA?e=0I6QRB

FreeRTOS ticking rate of 1000hz and other configs are critical. Be sure to use the default SDKconfig.defaults file included!

# ESP32 Bluetooth/BLE to PS/2 keyboard/mouse adapter

Project to adapt a Bluetooth or BLE keyboard and/or mouse to use on a computer with compatible PS/2 keyboard/mouse connector/s, wirelessly.
Note that big DIN 5 pin connectors ("AT" keyboard) and Mini-DINs (the violet ones) are equally supported.

YouTube demo: https://youtu.be/2PVjWfAAJFE

<p align="center">
  <img src="http://lsplab.com.ar/bt2ps2.jpg" width="700" title="Mini-DIN version">
</p>

# Compatibility

Tested Bluetooth Keyboards and Mice (Please add your own!): https://1drv.ms/x/s!AlKre4_rNmpJiYpl1v4KcbK1Pm77zA?e=0I6QRB

Working under latest ESP-IDF v5.3, compiled on Visual Studio Code. Multi-device support may not work on lower versions of the SDK.

**Developed and tested on the ESP32 DevKit rev 1 board, other variants may not work!**

WARNING: This project is for use in a plain ESP-32 module with BLE and BT Classic support and dual core processor. If you have another variant like C3, you'll have to adapt the code.

* ESP32 S3/C3 (BLE only boards): Check https://github.com/Hamberthm/esp32-bt2ps2/issues/3
* USB-HID instead of PS/2: Check https://github.com/Hamberthm/esp32-bt2ps2/issues/4

# Electrical connections

DIN and Mini-DIN connectors have similar pinout but in different arrangements.

Please use a multimeter and online info to make your cable. Beware of voltage present on the port, you can short and fry things!

Connections:

| ESP32 pin | PS/2 pin | example color | Notes                              |
| --------- | -------- | ------------- |  --------------------------------- |
| 23 or any | DATA     | orange        | Repeat on other pin for mouse bus  |
| 22 or any | CLK      | white         | Repeat on other pin for mouse bus  |
| GND       | GND      | black         | Always connect!                    |
| Vin       | +5v      | red           | Disconnect if using USB power!     |

You can change the DATA and CLK pins to whatever suits your fancy using these lines in the main.cpp file:

```cpp
const int KB_CLK_PIN = 22;
const int KB_DATA_PIN = 23;

const int MOUSE_CLK_PIN = 26;
const int MOUSE_DATA_PIN = 25;
```
Note: Pin 12 & 13 are ideal to solder a connector so you can use Vin, GND, 13 and 12 all in a row (ESP32 DevKit rev 1 boards). BEWARE that pin 12 is a strapping pin on the ESP32 and the module will FAIL to boot due to high signals from the PS/2 port. You can remove the strapping function of pin 12 by blowing an [eFuse](https://docs.espressif.com/projects/esptool/en/latest/esp32s2/espefuse/index.html) on your board. Use the following command:

```
python espefuse.py --port COM4 set_flash_voltage 3.3V
```

Also, applying voltage to pins 1 & 3 (U0 TXD and RXD) could cause the serial communication not to work, thus making serial console output impossible. Be careful on the ones you choose!

There is no need to connect the 5 volts from the port if you wish to power the board over USB. For debugging I recommend you leave it disconnected, as you can end up back-feeding 5 volts back to the PS/2 port, bad things can happen. Once all is working and you don't want to debug anymore, the 5 volts from the port are enough to power the board over the Vin (regulated) pin, making this a pretty neat standalone device!

NOTE: Don't leave the GND cable from the PS2 port floating, otherwise communication won't work! Always connect GND cable to the board even if you're using external power.

Note: ESP32 is **unofficially** 5V tolerant, so you can directly connect PS/2 pins to the board, that's my setup on my rev v1 board and I had no problems. However, you may use a logic level converter.

# Building and flashing

*Note there's a binary release available!

Project works as-is under Visual Studio Code (2024). You need to have the Espressif IDF extension installed and the v5.3 of the ESP-IDF SDK. Workflow is as follows:

1- Install Visual Studio Code

2- On the left, open the extensions panel

3- Search and install the Espressif IDF extension

4- Open the command prompt pressing Ctrl+Shift+P

5- Execute command "ESP-IDF: Configure ESP-IDF extension"

6- Select Express, and under "Select ESP-IDF version" choose v5.3

7- After installation, select File > Open Folder and open the ESP32-BT2PS2 project folder

8- Start building by pressing Ctrl+E and then B, or using the Build button in the bottom bar

9- If succesfully built, connect and flash your ESP32 board (Ctrl+E then F, or the flash button)

Refer to the following link for more instructions:

* https://github.com/espressif/vscode-esp-idf-extension/blob/master/docs/tutorial/install.md

Once succesfully built and flashed, you're ready to rock! (your BLE keyboard on an ancient computer, that is).


# Usage and debugging

Once powered up and first of all, the code will create and init an `esp32_ps2dev::PS2Keyboard` object, so it can start to talk to the computer as soon as possible. This is critical because during boot the BIOS can send different commands to the module to test the presence of the keyboard.

After PS/2 init, the module scans for nearby Bluetooth and BLE devices. If the last bonded keyboard is in range, it will try to connect to it using the keys stored on the NVS flash, so no pairing is needed for every connection. If it doesn't detect a previously bonded device, it will try to connect to the nearest keyboard in pairing mode. If both processes fail, it will wait one second and scan again until it finds anything.

IMPORTANT: Is recommended to do the pairing to the keyboard BEFORE connecing the board to a PS/2 port. For this, connect a charger or power bank to the ESP's USB port and pair your device more comfortably.

For v0.6, you can invoke pairing during execution (blue LED on) at any time. Press the "BOOT" button on the rev v1 board, essentially shorting GPIO_0 to ground. LED will go off and enter pairing mode. To cancel pairing, press and hold for 3 seconds (pairing aborting does not work on startup pairing).

**LED off: Pairing mode activated. LED on: normal execution, only previously paired devices can connect at any time.**

### >> BLE KEYBOARD OR MOUSE PAIRING: 
Set keyboard or mouse in pairing mode and power on the board. No code entry required.

### >> BLUETOOTH CLASSIC KEYBOARD PAIRING (code pairing):

1- Set keyboard in pairing mode and power on the board.

2- Wait until you see a short blast of quick flashes, then pay attention:

3- For each code digit, the board will flash the LED the number of times equal to the digit.

4- Press each digit on the keyboard as you read them (do not wait until it finishes! Some keyboards have a timeout).

5- If the digit is 0, a steady light will show for 1.5 seconds instead of the digit.

6- When you see the blast of quick flashes again, press ENTER.

OR you can always look at the code using the Serial monitor console, whatever you find easier, you fancy boy.

### >> BLUETOOTH CLASSIC KEYBOARD PAIRING, LEGACY PROCEDURE (legacy code pairing):

In older keyboards, the user must enter a custom code on the host device and then on the keyboard. Since we can't input it easily on the ESP32, the code is fixed to 1234.

1- Set keyboard in pairing mode and power on the board

2- Watch the Serial Ouput Console. Wait for the board finishing the scan and for the message "Waiting pairing code entry..."

3- Type 1234 on the keyboard then press ENTER.

Note: For Legacy Mode, no LED output is possible at the moment, so we need to check the serial console for the right time to enter the code. 

---

- If paired and working correctly, the board should blink the LED with each key press on the keyboard, or with each click on the mouse

Once connected you can start using your keyboard and mouse, blue LED should be on. Remove any USB power source and connect the board to the PS/2 compatible system and enjoy. Remember PS/2 is NOT a HOT-SWAP protocol, please only connect the board with the system totally OFF.

You can hot-disconnect the keyboard and mouse for models that support multiple devices hopping. The module will detect the disconnection and repeatedly try to reconnect while you're using other systems, so it will be back online as soon as the keyboard gets up to the ESP32 again. This is critical for keyboards/mouses that go to sleep and disconnect, or if you swap between computers using a multi-connection keyboard. 

Please note that pairing is only done after power up. If you wish to pair a new device, please reset the module with the reset button or power off and on the computer (not just reset it, because we need a power cycle). Note that if a previously paired device is on and in range, it will always connect to it first. Also invoke pairing after power on (LED on) by using the BOOT button or shorting GPIO_0 to ground.

In case something doesn't work, you'll need to debug. 

If the blue light on the module lights up and your keyboard connects, but it doesn't work, first of all reset your system. If it still doesn't work, then you'll need to enable debugging in the `esp32-ps2dev.cpp` file using `#DEFINE _ESP32_PS2DEV_DEBUG_`. Check for "PS/2 command received" messages and see where it hangs, or what the BIOS doesn't like. This is advanced so if you need help, make a new issue.

# TODO
 * Test on many keyboards and mouses.
 * Improve stability.
 * Clean up the code.
 * Test against various hosts.

# Reference
- http://www-ug.eecg.toronto.edu/msl/nios_devices/datasheets/PS2%20Protocol.htm
- http://www-ug.eecg.toronto.edu/msl/nios_devices/datasheets/PS2%20Mouse%20Protocol.htm
- http://www-ug.eecg.toronto.edu/msl/nios_devices/datasheets/PS2%20Keyboard%20Protocol.htm

# History
Using code and hard work from these folks:
 
 * https://playground.arduino.cc/ComponentLib/Ps2mouse/
 * https://github.com/grappendorf/arduino-framework/tree/master/ps2dev
 * https://github.com/dpavlin/Arduino-projects/tree/master/libraries/ps2dev
 * ps2 library Written by Chris J. Kiick, January 2008. https://github.com/ckiick
 * modified by Gene E. Scogin, August 2008.
 * modified by Tomas 'Harvie' Mudrunka 2019. https://github.com/harvie/ps2dev
 * modified for ESP32 by hrko 2022. https://github.com/hrko/esp32-ps2dev
 * fixed for PC compatibility and stability by Hambert, 2023. https://github.com/Hamberthm/esp32-ps2dev
 * bt-keyboard library by Guy Turcotte, 2022. https://github.com/turgu1/bt-keyboard
 * adapted to Arduino IDE and added improvements by Hambert, 2023. https://github.com/Hamberthm/bt-keyboard
