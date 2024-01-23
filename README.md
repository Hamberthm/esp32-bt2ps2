8/12/23: Remember to connect the PS2 port Ground to the board, even if you're using external power! It won't work if you leave it floating.

18/8/23: Release v0.4 is out with best compatibility as ever and many, many bug fixes!

26/6/23: PROJECT PORTED TO VS CODE, NOW BT CLASSIC WORKS (See "Building and flashing" below).

Working under latest ESP-IDF v5.1. **Developed and tested on the ESP32 DevKit rev 1 board, other variants may not work!**

FreeRTOS ticking rate of 1000hz is CRITICAL. Be sure to use the default SDKconfig file included!

WARNING: This project is for use in a plain ESP-32 module with BLE and Bt Classic support. If you have another variant, you'll have to adapt the code.

# ESP32 Bluetooth/BLE to PS/2 keyboard adapter

Project to adapt a Bluetooth or BLE keyboard to use on a computer with a compatible PS/2 keyboard connector, wirelessly.
Note that big DIN 5 pin connectors and Mini-DINs (the violet ones) are equally supported.

YouTube demo: https://youtu.be/2PVjWfAAJFE

<p align="center">
  <img src="http://lsplab.com.ar/bt2ps2.jpg" width="700" title="Mini-DIN version">
</p>

# Electrical connections

DIN and Mini-DIN connectors have similar pinout but in different arrangements.

Please use a multimeter and online info to make your cable. Beware of voltage present on the port, you can short and fry things!

Connections:

| ESP32 pin | PS/2 pin | example color |
| --------- | -------- | ------------- |
| 23 or any | DATA     | orange        |
| 22 or any | CLK      | white         |
| GND       | GND      | black         |
| Vin       | +5v      | red           |

You can change the DATA and CLK pins to whatever suits your fancy using these lines in the main.cpp file:

```cpp
const int CLK_PIN = 22;
const int DATA_PIN = 23;
```
Note: Pin 12 & 13 are ideal to solder a connector so you can use Vin, GND, 13 and 12 all in a row (ESP32 DevKit rev 1 boards). BEWARE that pin 12 is a strapping pin on the ESP32 and the module will FAIL to boot due to high signals from the PS/2 port. You can remove the strapping function of pin 12 by blowing an [eFuse](https://docs.espressif.com/projects/esptool/en/latest/esp32s2/espefuse/index.html) on your board. Use the following command:

```
python espefuse.py --port COM4 set_flash_voltage 3.3V
```

There is no need to connect the 5 volts from the port if you wish to power the board over USB. For debugging I recommend you leave it disconnected, as you can end up back-feeding 5 volts back to the PS/2 port, bad things can happen. Once all is working and you don't want to debug anymore, the 5 volts from the port are enough to power the board over the Vin (regulated) pin, making this a pretty neat standalone device!

8/12/23: NOTE: Don't leave the GND cable from the PS2 port floating, otherwise communication won't work! Always connect GND cable to the board even if you're using external power.

Note: ESP32 is **unofficially** 5V tolerant, so you can directly connect PS/2 pins to the board, that's my setup on my rev v1 board and I had no problems. However, it is ideal to use a logic level converter like this:

 * https://www.adafruit.com/product/757
 * https://www.ebay.com/itm/143385426765


# Building and flashing

*Note there's a binary release available!

Project works as-is under Visual Studio Code (2023). You need to have the Espressif IDF extension installed and the v5.1 of the ESP-IDF SDK. Workflow is as follows:

1- Install Visual Studio Code

2- On the left, open the extensions panel

3- Search and install the Espressif IDF extension

4- Open the command prompt pressing Ctrl+Shift+P

5- Execute command "ESP-IDF: Configure ESP-IDF extension"

6- Select Express, and under "Select ESP-IDF version" choose v5.1-rc2

7- After installation, select File > Open Folder and open the ESP32-BT2PS2 project folder

8- Start building by pressing Ctrl+E and then B, or using the Build button in the bottom bar

9- If succesfully built, connect and flash your ESP32 board (Ctrl+E then F, or the flash button)

Refer to the following link for more instructions:

* https://github.com/espressif/vscode-esp-idf-extension/blob/master/docs/tutorial/install.md

Once succesfully built and flashed, you're ready to rock! (your BLE keyboard on an ancient computer, that is).


# Usage and debugging

Once powered up and first of all, the code will create and init an `esp32_ps2dev::PS2Keyboard` object, so it can start to talk to the computer as soon as possible. This is critical because during boot the BIOS can send different commands to the module to test the presence of the keyboard.

After PS/2 init, the module scans for nearby Bluetooth and BLE devices. If the last bonded keyboard is in range, it will try to connect to it using the keys stored on the NVS flash, so no pairing is needed for every connection. If it doesn't detect a previously bonded device, it will try to connect to the nearest keyboard in pairing mode. If both processes fail, it will wait one second and scan again until it finds anything.

BLE KEYBOARD PAIRING: Set keyboard in pairing mode and power on the board. No code entry required.

BLUETOOTH CLASSIC KEYBOARD PAIRING (code pairing):

1- Set keyboard in pairing mode and power on the board

2- Wait until you see a short blast of quick flashes, then pay attention:

3- For each code digit, the board will flash the LED the number of times equal to the digit

4- Press each digit on the keyboard as you read them (do not wait until it finishes! Some keyboards have a timeout)

5- If the digit is 0, a steady light will show for 1.5 seconds instead of the digit

6- When you see the blast of quick flashes again, press ENTER

OR you can always look at the code using the Serial monitor console, whatever you find easier, you fancy boy.

Once connected you can start using your keyboard, blue LED should be on. Connect the board to the computer or PS/2 compatible system and enjoy.

You can hot-disconnect the keyboard. The module will detect the disconnection and repeatedly try to reconnect, so it will be back online as soon as the keyboard gets up again. This is critical for keyboards that go to sleep and disconnect, or if you swap between computers using a multi-connection keyboard. 

Please note that pairing is only done after power up. If you wish to pair a new device, please reset the module with the reset button or power off and on the computer (not just reset it, because we need a power cycle). Note that if a previously paired device is on and in range, it will always connect to it first.

In case something doesn't work, you'll need to debug. 

If the blue light on the module lights up and your keyboard connects, but it doesn't work, then you'll need to enable debugging in the `esp32-ps2dev.cpp` file using `#DEFINE _ESP32_PS2DEV_DEBUG_ *something*`. Check for "PS/2 command received" messages and see where it hangs, or what the BIOS doesn't like. 

# TODO
 * Test on many keyboards.
 * Improve stability.
 * Clean up the code.
 * Test against various hosts.
 * Add mouse support.

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
