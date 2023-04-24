# ESP32 Bluetooth/BLE to PS/2 keyboard adapter

Project to adapt a Bluetooth or BLE keyboard to use on a computer with a compatible PS/2 keyboard connector, wirelessly.
Note that big DIN 5 pin connectors and Mini-DINs (the violet ones) are equally supported.


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

You can change the DATA and CLK pins to whatever suits your fancy using these lines in the .ino file:

```cpp
const int CLK_PIN = 22;
const int DATA_PIN = 23;
```

There is no need to connect the 5 volts from the port if you wish to power the board over USB. For debugging I recommend you leave it disconnected. Once all is working and you don't want to debug anymore, the 5 volts from the port are enough to power the board over the Vin (regulated) pin, making this a pretty neat standalone device!

Note: ESP32 is **unofficially** 5V tolerant, so you can directly connect PS/2 pins to the board, that's my setup on my rev v1 board and I had no problems. However, it is ideal to use a logic level converter like this:

 * https://www.adafruit.com/product/757
 * https://www.ebay.com/itm/143385426765


# Building and flashing

Project works as-is under Arduino IDE. You need to have the latest Arduino Core for ESP32 installed. Refer to the following link for instructions, I recommend the manual installation:

* https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html

You can easily port it back to ESP32-IDF if you want. Just make the changes from the 'setup()'and 'loop()' functions to the structure IDF uses as entry point and format it accordingly. All or at least most of the header files should just work fine.

Once succesfully built and flashed, you're ready to rock! (your BLE keyboard on an ancient computer, that is).


# Usage and debugging

Once powered up and first of all, the code will create and init an 'esp32_ps2dev::PS2Keyboard' object, so it can start to talk to the computer as soon as possible. This is critical because during boot the BIOS can send different commands to the module to test the presence of the keyboard.

After PS/2 init, the module scans for nearby Bluetooth and BLE devices. If the last bonded keyboard is in range, it will try to connect to it using the keys stored on the NVS flash, so no pairing is needed for every connection. If it doens't detect a previously bonded device, it will try to connect to the nearest keyboard in pairing mode. If both processes fail, it will wait one second and scan again until it finds anything.

Once connected you can start using your keyboard, blue LED should be on.

You can hot-disconnect the keyboard. The module will detect the disconnection and repeatedly try to reconnect, so it will be back online as soon as the keyboard gets up again. This is critical for keyboards that go to sleep and disconnect, or if you swap between computers using a multi-connection keyboard. 

Please note that pairing is only done after power up. If you wish to pair a new device, please reset te module with the reset button or the computer. Note that if a previously paired device is on and in range, it will always connect to it first.

In case something doesn't work, you'll need to debug. 

If the blue light on the module lights up and your keyboard connects, but it doesn't work, then you'll need to enable debugging in the 'esp32-ps2dev.cpp' file using '#DEFINE _ESP32_PS2DEV_DEBUG_ serial'. Check for "PS/2 command received" messages and see where it hangs, or what the BIOS doesn't like. 

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
