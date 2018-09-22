MCC-libusb
==========

Interface to MeasurementComputing devices using libusb for cross-platform usability.

# Introduction

This is a linux interface for MeasurementComputing DAQs found [here](http://kb.mccdaq.com/KnowledgebaseArticle50047.aspx).

I have made a few modifications (see commit history).

1. Added support for USB-1608FS-PLUS
2. Removed some redundant code.
3. Removed need for stringutil.h. Moved datatypesandstatics.h into mccdevice.h
4. Per-channel gain and slope. Moved these into device properties.
5. Got rid of asynchronous polling (for now) because it relied on POSIX-only threading.
6. Removed all the firmware flashing because the firmware location was hard-coded.

TODO:

4. Setters and getters for channels/rate/range
5. Platform-independent threading for asynchronous polling.

# How to use it in Mac OS X

1. Install [libusb-1.0](http://libusb.info/). On OSX use homebrew, it is easier.
2. Include mccdevice.h into your project. `#include "mccdevice.h"`
3. Compile your project.

	1. SynchTest on OSX
        1. Compile A (libusb is on your path): `g++ -std=c++11 -o synchTest synchTest.cpp mccdevice.cpp -lusb-1.0`
        2. OR Compile B (libusb is not on your path): `g++ -std=c++11 -o synchTest synchTest.cpp mccdevice.cpp -I/usr/local/include/libusb-1.0/ -L/usr/local/lib -lusb-1.0`
        3. Test: `./synchTest`

    2. LabStreamingLayer (OSX)
        1. Compile: `g++ -std=c++11 -o lslTest lslTest.cpp mccdevice.cpp -I/usr/local/include/libusb-1.0/ -I./lsl -L/usr/local/lib -lusb-1.0 -L./lsl -llsl64`
        2. Test: `./lslTest`. Note that liblsl64.(so|dylib|a|lib|dll) must be installed to run. It is not sufficient to copy it locally.

    3. ReceiveData on OSX
    	1. Compile: `g++ -o runViewReceive ViewReceiveData.cpp -I ./lsl -L/usr/local/lib -llsl64`
    	2. Test: `./runReceive`. Note that liblsl64.(so|dylib|a|lib|dll) must be installed to run. It is not sufficient to copy it locally.

    4. DAQ and LabStreamingLayer on OSX
        1. Compile: `g++ -std=c++11 -o testLSLSync lslSync.cpp mccdevice.cpp -I/usr/local/include/libusb-1.0/ -I./lsl -L/usr/local/lib -lusb-1.0 -L./lsl -llsl64`
        2. Test: `./testLSLSync`. 

# How to use it in Linux - Ubuntu 13.10

1. Install [libusb-1.0](http://libusb.info/) using `sudo apt-get install libusb-1.0`
2. Include mccdevice.h into your project. `#include "mccdevice.h"`
3. Compile your project:

    1. SynchTest on Linux
        1. Compile A (libusb - in the path): `g++ -std=c++11 -o synchTest synchTest.cpp mccdevice.cpp -lusb-1.0` 
           or 
           Compile B (libusb - not in the path): `g++ -std=c++11 -o syncTest synchTest.cpp mccdevice.cpp -I /usr/include/libusb-1.0/ -L /usr/lib/x86_64-linux-gnu/ -lusb-1.0`
        2. Test: `./synchTest`

    2. LabStreamingLayer on Linux
        1. Include: liblsl64.so must be in the building directory of the project
        2. Compile: `g++ -std=c++11 -o testLSL lslTest.cpp mccdevice.cpp -I /usr/include/libusb-1.0/ -I ./lsl -L /usr/lib/x86_64-linux-gnu/ -lusb-1.0 -L ./ -llsl64`
        3. Test: `./lslTest`

    3. ReceiveData on Linux
        1. Include: liblsl64.so must be in the building directory of the project
        2. Compile: `g++ -o runViewReceive ViewReceiveData.cpp -I ./lsl -L ./ -llsl64`
        3. Test: `./runReceive`

    4. DAQ and LabStreamingLayer on Linux
        1. Include: liblsl64.so must be in the building directory of the project
        2. Compile: `g++ -std=c++11 -o testLSLSync lslSync.cpp mccdevice.cpp -I /usr/include/libusb-1.0/ -I ./lsl -L /usr/lib/x86_64-linux-gnu/ -lusb-1.0 -L ./ -llsl64`
        3. Test: `./testLSLSync`. 
