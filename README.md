# LSL-MeasurementComputing
(Unsupported) Labstreaminglayer application for Measurement Computing device(s).

This application was pulled out of another private repository (ExperimentSystem).
It is no longer in use and only very limited support is available.

## Build

`mkdir build`

`cd build`

`cmake ..` or, in Windows, `cmake .. -G "Visual Studio 12 Win64"`

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
