Download measurements from Beurer GL50 EVO using an ESP32 and Bluetooth Low Energy (BLE)

With this device one can use either a PC app (Windows) via USB, or an Android app that uses Bluetooth Low Energy (Beurer Health Manager). The later downloads measurements from the device and displays graphs. Measurements are stored locally on the smartphone, and can be displayed.

This project is a rewrite of a previous one that uses ESP32 BLE Arduino library.
NimBLE is modern and actively maintained.
I wanted to download GL50 EVO data using a Macbook.
The simplest way I found to do that is to use an ESP32 that has BLE, and print data in the Serial.
The sketch needs first to be uploaded and second to be executed for getting data.
To upload the sketch I have used:
1 - PlatformIO on MacOS (this can be done on Mac, Windows, or Linux)
2 - Arduino IDE (this can also be done on Mac, Windows, or Linux)
For Arduino IDE just replace .cpp by .ino
When running the sketch, we only need a serial console.
With PlatformIO we can use the command pio device monitor, but we can also use any other app. One I love is CoolTerm from Roger Meier: https://freeware.the-meiers.org
Don't forget to modify the "if (advertisedDevice.haveName() && advertisedDevice.getName() == "Beurer GL50EVO" && advertisedDevice.getAddress().toString() == "ed:ac:3e:ea:54:ff")" line to suit your situation.
