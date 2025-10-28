# IoT Sensor Logger with FlashDB - Project Architecture

## Overview

This document outlines the complete architecture and implementation plan for a Touch Sensor Logger project using FlashDB on ESP32-S3 WiFi LoRa 32 V3 microcontroller. The project demonstrates practical embedded database usage, unit testing on hardware, and USB debugging techniques.

**Project Goals:**

- Learn FlashDB library usage for persistent touch event logging
- Build experience with ESP32-S3 development using ESP-IDF framework
- Implement capacitive touch sensor detection on 7 pins
- Record timestamped touch events with user identification
- Create web UI for viewing touch logs and CSV export
- Implement unit testing directly on microcontroller hardware
- Set up USB debugging for real-time code inspection

**Key Components:**

- **ESP32-S3 WiFi LoRa 32 V3** microcontroller board
- **FlashDB** embedded database (KVDB for configuration, TSDB for touch event logs)
- **Capacitive Touch Sensors** on 7 ESP32 pins (GPIO 4,5,6,7,8,9,10)
- **Web UI** served from ESP32 flash memory (HTML/CSS/JS files stored in SPIFFS)
- **REST API** endpoints for FlashDB operations and CSV export
- **WiFi connectivity** for remote access via access point
- **Hardware unit testing** with Unity framework
- **USB debugging** via OpenOCD (not JTAG)

**Limitations & Constraints:**

- Cannot perform complex browser-side calculations (limited ESP32 resources)
- Web UI must be lightweight and static (no server-side rendering)
- Real-time sensor data requires efficient storage/retrieval
- Hardware testing limited to ESP32 capabilities

## System Architecture

### Hardware Architecture

- **Microcontroller**: ESP32-S3 WiFi LoRa 32 V3 (240MHz dual-core, 8MB flash, 320KB RAM)
- **Flash Memory**: 8MB total, partitioned for:
  - Application firmware (1MB)
  - SPIFFS filesystem for web UI (2MB)
  - FlashDB partition (32KB for KVDB/TSDB)
  - NVS and PHY data
- **Connectivity**: WiFi (2.4GHz), Bluetooth 5.0, LoRa radio module

### Software Architecture

#### Core Components

1. **ESP-IDF Framework**: Native Espressif framework (not Arduino) for better flash control
2. **FlashDB Library**: Embedded database with FAL (Flash Abstraction Layer)
   - **KVDB**: Key-value storage for configuration and metadata
   - **TSDB**: Time-series database for sensor readings
3. **FAL Library**: `armink/FAL@^0.5.0` - Provides headers for FlashDB's flash operations
4. **Custom FAL Port**: `fal_flash_esp32_port.c` - ESP32-specific flash implementation
5. **SPIFFS Filesystem**: Stores compressed web UI files (HTML/CSS/JS)

#### Web Architecture

- **Client-Side Rendering**: Web UI runs entirely in browser (ESP32 serves static files only)
- **RESTful API**: HTTP endpoints for FlashDB CRUD operations
- **Data Flow**:
  1. User connects to ESP32's WiFi AP or joins network
  2. Browser requests index.html from SPIFFS
  3. JavaScript fetches data via REST API
  4. Charts/visualizations render client-side
- **Benefits**: Minimal ESP32 processing, responsive UI, standard web development

#### Testing & Debugging

- **Unity Framework**: Hardware-based unit testing
- **OpenOCD/JTAG**: USB debugging with breakpoints and variable inspection
- **PlatformIO Integration**: Automated build/flash/monitor workflow

## Prerequisites

- **Hardware**: ESP32-S3 WiFi LoRa 32 V3 board (Health Tech Automation)
- **Software**: PlatformIO IDE with ESP-IDF framework
- **Tools**: USB cable (for flashing, serial monitoring, and debugging), 7 touch sensors (capacitive touch pads connected to GPIO pins 4-10)
- **Knowledge**: C programming, basic ESP32 development, REST APIs

## Implementation Status

### Current Status (Completed)

- [x] ESP32-S3 WiFi LoRa 32 V3 board setup with ESP-IDF
- [x] FlashDB library installation (`kuba2k2/FlashDB@^2024.12.30`)
- [x] Custom FAL porting layer for ESP32 flash operations
- [x] Basic KVDB example with persistent boot count
- [x] Custom partition table (32KB flashdb partition)
- [x] Successful firmware build and flash to hardware
- [x] Serial monitoring working (115200 baud)
- [x] CMakeLists.txt filtering to exclude C++ warnings

### In Progress

- [ ] WiFi connectivity setup for web server (Access Point mode)
- [ ] SPIFFS filesystem configuration for web UI storage
- [ ] HTTP server implementation with CSV export endpoint
- [ ] REST API endpoints for FlashDB operations and touch log retrieval
- [ ] Web UI files (HTML/CSS/JS) creation and compression
- [ ] Capacitive touch sensor initialization on 7 pins (GPIO 4-10)
- [ ] Touch detection task with timestamped event logging
- [ ] Web-based touch event log viewer with CSV download

### Planned

- [ ] Hardware unit testing with Unity framework
- [ ] USB debugging setup with OpenOCD/JTAG
- [ ] Performance optimization and error handling
- [ ] Documentation updates and workflow automation

---

## Section 1: Project Setup

### Step 1.1: Initialize PlatformIO Project

1. Open PlatformIO IDE
2. Create new project: `File > New Project`
3. Configure:
   - Project Name: `flashDB_example`
   - Board: Search for "esp32-s3-devkitc-1" (closest match for WiFi LoRa 32 V3)
   - Framework: Arduino
   - Location: Current workspace directory
4. Click "Finish"

**Error Handling**: If board not found, manually edit `platformio.ini` and add:

```
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
```

### Step 1.2: Install FlashDB Library

1. In `platformio.ini`, add to the env section:

```
lib_deps =
    kuba2k2/FlashDB @ ^2.0.0
```

2. Save file and run `PlatformIO: Rebuild IntelliSense Index` from command palette

**Error Handling**: If installation fails, check PlatformIO logs or manually install via `pio lib install kuba2k2/FlashDB`

### Step 1.3: Verify Setup

1. Build project: `PlatformIO: Build` (Ctrl+Alt+B)
2. Connect ESP32-S3 via USB
3. Upload: `PlatformIO: Upload` (Ctrl+Alt+U)

**Checkpoint**: Serial monitor (`PlatformIO: Serial Monitor`) should show "Hello World" or basic output

---

## Section 2: Basic Examples

### Step 2.1: Key-Value Storage Demo

1. Create `src/kv_demo.cpp`:

```cpp
#include <Arduino.h>
#include <flashdb.h>

fdb_kvdb_t kvdb;

void setup() {
    Serial.begin(115200);

    // Initialize FlashDB KVDB (adjust partition as needed)
    fdb_kvdb_init(&kvdb, "env", "fdb_kvdb1", NULL, NULL);

    // Store values
    fdb_kv_set_blob(&kvdb, "temp", "25.5");
    fdb_kv_set_blob(&kvdb, "humidity", "60");

    // Retrieve and print
    char value[32];
    fdb_kv_get_blob(&kvdb, "temp", value, sizeof(value));
    Serial.println(value);
}

void loop() {
    delay(1000);
}
```

2. Set as main file in `platformio.ini`: `src_filter = +<kv_demo.cpp>`
3. Build and upload

**Error Handling**: If init fails, check flash partition configuration in ESP32 menuconfig

**Checkpoint**: Serial output shows stored values

### Step 2.2: Time Series Data Storage Demo

1. Create `src/ts_demo.cpp`:

```cpp
#include <Arduino.h>
#include <flashdb.h>

fdb_tsdb_t tsdb;

void setup() {
    Serial.begin(115200);

    // Initialize TSDB
    fdb_tsdb_init(&tsdb, "sensor", "fdb_tsdb1", NULL, NULL);

    // Add time series data
    fdb_tsdb_append(&tsdb, 1, "25.5");  // timestamp, value
    fdb_tsdb_append(&tsdb, 2, "26.0");
}

void loop() {
    // Query recent data
    fdb_blob_t blob;
    fdb_tsdb_query(&tsdb, 0, fdb_tsdb_query_count(&tsdb), &blob);
    Serial.println((char*)blob.buf);
    delay(5000);
}
```

2. Update `platformio.ini` src_filter
3. Build and upload

**Error Handling**: Ensure sufficient flash space for TSDB

**Checkpoint**: Time series data prints correctly

---

## Section 3: Small IoT Project - Sensor Data Logger

### Step 3.1: Implement Sensor Logger

1. Add DHT sensor library to `platformio.ini`:

```
lib_deps =
    kuba2k2/FlashDB @ ^2.0.0
    adafruit/DHT sensor library @ ^1.4.4
```

2. Create `src/sensor_logger.cpp`:

```cpp
#include <Arduino.h>
#include <flashdb.h>
#include <DHT.h>

#define DHTPIN 4
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);
fdb_tsdb_t tsdb;

void setup() {
    Serial.begin(115200);
    dht.begin();

    fdb_tsdb_init(&tsdb, "sensors", "fdb_tsdb1", NULL, NULL);
}

void loop() {
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();

    if (!isnan(temp) && !isnan(hum)) {
        char data[32];
        sprintf(data, "%.1f,%.1f", temp, hum);
        fdb_tsdb_append(&tsdb, millis(), data);
        Serial.println(data);
    }
    delay(30000);  // 30 second intervals
}
```

3. Build and upload

**Error Handling**: Connect DHT11 to GPIO 4, check sensor wiring if readings fail

**Checkpoint**: Serial shows temperature/humidity data logging

### Step 3.2: Add Data Retrieval Web Interface

1. Add WiFi and web server libraries
2. Extend code to serve data via HTTP endpoint

**Checkpoint**: Access data via browser at ESP32 IP

---

## Section 4: Unit Testing on Hardware

### Step 4.1: Set Up Unity Testing Framework

1. Add to `platformio.ini`:

```
lib_deps =
    kuba2k2/FlashDB @ ^2.0.0
    Unity @ ^2.5.2
```

2. Enable test environment:

```
[env:test]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
lib_deps = Unity @ ^2.5.2 kuba2k2/FlashDB @ ^2.0.0
build_flags = -D UNITY_INCLUDE_CONFIG_H
```

### Step 4.2: Create Hardware Tests

1. Create `test/test_flashdb.cpp`:

```cpp
#include <unity.h>
#include <flashdb.h>

fdb_kvdb_t test_kvdb;

void setUp() {
    fdb_kvdb_init(&test_kvdb, "test", "fdb_kvdb_test", NULL, NULL);
}

void test_kv_operations() {
    // Test set/get
    fdb_kv_set_blob(&test_kvdb, "test_key", "test_value");
    char value[32];
    fdb_kv_get_blob(&test_kvdb, "test_key", value, sizeof(value));
    TEST_ASSERT_EQUAL_STRING("test_value", value);
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_kv_operations);
    UNITY_END();
}

void loop() {}
```

2. Run tests: `PlatformIO: Test` (Ctrl+Alt+T)

**Error Handling**: Tests run on hardware, monitor serial for results

**Checkpoint**: All tests pass, serial shows "OK" for each test

---

## Section 5: USB Debugging Setup

### Step 5.1: Configure JTAG Debugging

1. Install OpenOCD: `pio platform install espressif32 --with-package framework-arduinoespressif32`
2. In `platformio.ini`, add:

```
[env:debug]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
debug_tool = esp-prog
debug_init_break = tbreak setup
```

3. Connect JTAG debugger to ESP32-S3 pins

### Step 5.2: Debug Session

1. Set breakpoints in FlashDB code (e.g., in `fdb_kv_set_blob`)
2. Start debug: `PlatformIO: Start Debugging` (F5)
3. Step through code, inspect variables

**Error Handling**: Ensure ESP32-S3 has JTAG pins exposed

**Checkpoint**: Breakpoints trigger, variables inspectable

---

## Section 6: Automated Workflow Documentation

### Step 6.1: Create Workflow Scripts

1. Add to `platformio.ini`:

```
[env:flash-monitor]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
; Custom target for flash + monitor
extra_scripts = post:flash_and_monitor.py
```

2. Create `flash_and_monitor.py`:

```python
import subprocess
ImportExtraScript

def flash_and_monitor(source, target, env):
    # Flash
    env.Execute("pio run -t upload")
    # Monitor
    env.Execute("pio device monitor")
```

### Step 6.2: Usage Guide

- Flash and monitor: `pio run -e flash-monitor`
- Debug: `pio debug`
- Test: `pio test`

**Checkpoint**: Automated commands work end-to-end

---

## Final Validation

Run through all checkpoints. If issues persist, check ESP32-S3 documentation and FlashDB GitHub issues for ESP32-S3 specific configurations.

## Commit Message

"feat: Complete FlashDB ESP32-S3 learning project with KV/TSDB demos, IoT logger, unit tests, and USB debugging setup"
