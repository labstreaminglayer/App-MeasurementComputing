# MCCOutlet

Streams analog input data from [Measurement Computing](https://www.mccdaq.com/) (MCC) USB DAQ devices into [Lab Streaming Layer](https://labstreaminglayer.org/) (LSL). Supports both a Qt6 GUI application and a headless CLI for servers and automated workflows.

## Features

- Continuous hardware-paced acquisition via `ulAInScan` (no dropped samples)
- **Scaled mode**: calibrated voltage output as `cf_float32`
- **Raw mode**: integer ADC counts matching device resolution (`cf_int16` for 12/16-bit, `cf_int32` for 18/24-bit), offset by the midpoint (0V = 0).
- LSL stream metadata includes voltage range, resolution, and scaling coefficients for offline reconstruction
- Automatic FIFO overrun recovery (restarts scan transparently on USB scheduling delays)
- Per-device capability queries: supported voltage ranges, resolution, max scan rate
- Configuration file support (`.cfg`) for persistent settings
- macOS code signing and notarization for distribution

## Supported Devices

Any MCC USB DAQ device supported by [uldaq](https://github.com/mccdaq/uldaq) that provides analog input scanning.
See the Digilent-provided [list here](https://files.digilent.com/manuals/Linux-hw.pdf).

Tested with:

- USB-1608FS-Plus (16-bit, 8 SE channels, 400 kHz aggregate)

Use `--list-devices` (CLI) or the device dropdown (GUI) to see connected devices, and `--list-ranges` to query capabilities.

## Project Structure

```
App-MeasurementComputing/
├── CMakeLists.txt              # Root build configuration
├── MCCOutlet.cfg               # Default configuration file
├── app.entitlements            # macOS network/USB capabilities
├── cmake/
│   └── Uldaq.cmake             # uldaq ExternalProject build
├── src/
│   ├── core/                   # Qt-independent core library
│   │   ├── include/mccoutlet/
│   │   │   ├── Device.hpp          # MCC device interface (uldaq wrapper)
│   │   │   ├── LSLOutlet.hpp       # LSL outlet with format selection
│   │   │   ├── Config.hpp          # Configuration management
│   │   │   └── StreamThread.hpp    # Background streaming thread
│   │   └── src/
│   ├── cli/                    # Headless CLI application
│   │   └── main.cpp
│   └── gui/                    # Qt6 GUI application
│       ├── MainWindow.hpp/cpp
│       ├── MainWindow.ui
│       └── main.cpp
├── scripts/
│   └── sign_and_notarize.sh    # macOS signing script
└── .github/workflows/
    └── build.yml               # CI/CD workflow
```

## Building

### Prerequisites

- CMake 3.28+
- C++20 compiler (Clang 15+, GCC 12+)
- **Autotools**: `autoconf`, `automake`, `libtool` (for building uldaq)
- **libusb-1.0**
- Qt 6.8 (for GUI build; optional)

#### macOS

```bash
brew install autoconf automake libtool libusb cmake
# For GUI:
brew install qt@6
```

#### Ubuntu/Debian

```bash
sudo apt-get install build-essential autoconf automake libtool libusb-1.0-0-dev cmake
# For GUI:
# Qt 6.8 is not in default repos — use aqtinstall:
pip install aqtinstall
aqt install-qt linux desktop 6.8.3 gcc_64 -O ~/Qt
export CMAKE_PREFIX_PATH=~/Qt/6.8.3/gcc_64
sudo apt-get install libgl1-mesa-dev libxkbcommon-dev libxcb-cursor0
```

### Quick Start

```bash
git clone https://github.com/labstreaminglayer/App-MeasurementComputing.git
cd App-MeasurementComputing

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

cmake --install build --prefix build/install
```

uldaq is fetched and built automatically from source (via `ExternalProject`). No system-wide uldaq installation is needed.

### Build Options

| Option                 | Default | Description                                      |
|------------------------|---------|--------------------------------------------------|
| `MCCOUTLET_BUILD_GUI`  | ON      | Build the GUI application (requires Qt6)         |
| `MCCOUTLET_BUILD_CLI`  | ON      | Build the CLI application                        |
| `ULDAQ_GIT_TAG`        | v1.2.1  | uldaq version to fetch from GitHub               |
| `LSL_INSTALL_ROOT`     | -       | Path to installed liblsl (skips FetchContent)    |
| `LSL_FETCH_REF`        | v1.17.4 | liblsl git ref to fetch                          |

### CLI-Only Build

For headless systems or servers:

```bash
cmake -S . -B build -DMCCOUTLET_BUILD_GUI=OFF
cmake --build build --parallel
```

## Usage

### GUI Application

```bash
./MCCOutlet                    # Use default config
./MCCOutlet myconfig.cfg       # Use custom config
```

Select the device from the dropdown, configure channels, sample rate, voltage range, and data format (Scaled or Raw), then click **Link** to start streaming.

### CLI Application

```bash
MCCOutletCLI [options]

Options:
  -h, --help              Show help
  -l, --list-devices      List connected MCC devices and exit
  --list-ranges           Show device capabilities (ranges, resolution, rate limits)
  -c, --config FILE       Load configuration from FILE
  -n, --name NAME         Stream name (default: MCCDaq)
  -t, --type TYPE         Stream type (default: RawBrainSignal)
  -d, --device INDEX      Device index (default: 0)
  --device-name NAME      Select device by product name (substring match)
  --low-chan N             Low channel (default: 0)
  --high-chan N            High channel (default: 5)
  -r, --rate RATE         Sample rate in Hz (default: 16384)
  --range VALUE           Voltage range (uldaq Range enum value, default: auto)
  --raw                   Output raw integer ADC instead of scaled voltage
```

Examples:

```bash
# List devices
MCCOutletCLI --list-devices

# Query capabilities
MCCOutletCLI --list-ranges -d 0

# Stream 6 channels at 16384 Hz (scaled voltage)
MCCOutletCLI -d 0 --low-chan 0 --high-chan 5 --rate 16384

# Stream raw ADC integers
MCCOutletCLI --raw --device-name USB-1608FS

# Use a config file
MCCOutletCLI -c MCCOutlet.cfg
```

### Configuration File

```ini
# MCCOutlet.cfg
[Stream]
name=MCCDaq
type=RawBrainSignal

[Device]
device_index=0
low_channel=0
high_channel=5
sample_rate=16384
# range=6
# scaled=1
```

## Data Formats and LSL Stream Metadata

### Scaled Mode (default)

Outputs calibrated voltage as `cf_float32`. Channel units are volts (`V`).

### Raw Mode (`--raw`)

Outputs uncalibrated ADC offset by midpoint (0=0). The LSL channel format is selected based on the device's ADC resolution:

| ADC Resolution | LSL Format  | Value Range           |
|----------------|-------------|-----------------------|
| 12-bit         | `cf_int16`  | -2048 - 2047          |
| 16-bit         | `cf_int16`  | -32768 - 32767        |
| 18-bit         | `cf_int32`  | -116072 - 116071      |
| 24-bit         | `cf_int32`  | -83886078 - 83886077  |

### Stream Metadata

The LSL stream's XML description always includes an `<acquisition>` block with scaling metadata, regardless of mode:

```xml
<acquisition>
  <resolution>16</resolution>
  <scaled>false</scaled>
  <range_min>-10</range_min>
  <range_max>10</range_max>
  <range_label>+/-10V</range_label>
  <scaling_slope>0.000305176</scaling_slope>
  <scaling_offset>-10</scaling_offset>
</acquisition>
```

To convert raw counts to voltage: `voltage = count * scaling_slope + scaling_offset`

## Technical Notes

### Sample Rate Quantization

MCC USB devices derive their sample clock from a fixed crystal (e.g., 40 MHz on the USB-1608FS-Plus). The actual rate is `clock / integer_divisor`, so requesting 16384 Hz may yield 16386.73 Hz. The LSL stream's nominal rate reflects the device's actual rate.

### FIFO Overrun Recovery

USB Full Speed devices have limited hardware FIFOs (e.g., 64 KB on the USB-1608FS-Plus). If macOS USB scheduling delays cause the FIFO to overflow, MCCOutlet automatically stops and restarts the scan, logging a warning. No user intervention is needed.

### Channel Count

The number of available channels depends on the device and its input mode. The USB-1608FS-Plus provides 8 single-ended channels (0-7). If `high_channel` exceeds the device's maximum, it is silently clamped.

### Timestamp Delay

Using scripts/audio_latency_test, I characterized that the USB-1608FS-Plus timestamps audio tones about **1.1 msec** after the audio callback time. This delay could either be due to a delay in the audio hardware layer or in the DAQ buffer, or a combination of both. These delays exist in all devices and are generally unknowable but can be characterized using hardware synchronizing the different devices. If someone identifies consistent delays in the hardware then please let me know, and I can subtract these delays from the timestamps at the source.

## macOS Code Signing

For local development, the build applies ad-hoc signing with USB and network entitlements.

For distribution, see `scripts/sign_and_notarize.sh`. The GitHub Actions workflow handles signing and notarization automatically on release using organization secrets:

| Secret                             | Description                                                |
|------------------------------------|------------------------------------------------------------|
| `PROD_MACOS_CERTIFICATE`           | Base64-encoded Developer ID Application certificate (.p12) |
| `PROD_MACOS_CERTIFICATE_PWD`       | Certificate password                                       |
| `PROD_MACOS_NOTARIZATION_APPLE_ID` | Apple ID email for notarization                            |
| `PROD_MACOS_NOTARIZATION_PWD`      | App-specific password for notarytool                       |
| `PROD_MACOS_NOTARIZATION_TEAM_ID`  | Apple Developer Team ID                                    |

## License

MIT License - see [LICENSE](LICENSE)
