# Compatibility

Tested Bluetooth Keyboards, Mice and ESP Chiptype combinations

| ESP-ID        	| Peripheral 	| Brand 	| Model            	| By User                    	| Notes                    	|
|---------------	|------------	|-------	|------------------	|----------------------------	|----------------------------	|
| ESP32-D0WD-V3 	| Keyboard   	| Dell  	| KB700 (KB7221Wt) 	| [@RandomFunctionForUsername](https://github.com/RandomFunctionForUsername) 	| On keyboard channel 2 or 3 	|

Your combination here? Edit this table and make a pull request!

## Other Notes
Working under latest ESP-IDF v5.3.0 (not v5.3.3 or higher), compiled on Visual Studio Code. Multi-device support may not work on lower versions of the SDK.

**Developed and tested on the ESP32 DevKit rev 1 board, other variants may not work!**

WARNING: This project is for use in a plain ESP-32 module with BLE and BT Classic support and dual core processor. If you have another variant like C3, you'll have to adapt the code.

* ESP32 S3/C3 (BLE only boards): [Check](https://github.com/Hamberthm/esp32-bt2ps2/issues/3)
* USB-HID instead of PS/2: [Check](https://github.com/Hamberthm/esp32-bt2ps2/issues/4)
