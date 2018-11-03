# LSL-MeasurementComputing

(Unsupported) Labstreaminglayer application for Measurement Computing device(s).

This application was pulled out of another private repository (SachsLab/ExperimentSystem).
It is no longer in use and only very limited support is available.

## Build

`mkdir build && cd build`

Configure:
    * Linux: `cmake ..`
    * MacOS: `cmake .. -DQt5_DIR=$(brew --prefix qt)/lib/cmake/Qt5/`
    * Windows: `cmake .. -G "Visual Studio 14 2015 Win64" -DLIBUSB_ROOT="D:\Tools\Misc\libusb" -DQt5_DIR="C:\Qt\5.11.1\msvc2015_64\lib\cmake\Qt5"

`make`

In linux, it will be further necessary to modify the udev rules so that the program can access the device without root permissions. (See Q7 [here](ftp://lx10.tx.ncsu.edu/pub/Linux/drivers/README))

`sudo cp ../61-mcc.rules /etc/udev/rules.d`

`sudo udevadm trigger`

## Use

Run the application (e.g. `./MCCOutlet`)

This will create a LSL stream called MCCDaq.
It has 6 channels.
It is sampling at 16384 Hz.
This stream will push 512 float samples on every frame at 32 frames per second.
