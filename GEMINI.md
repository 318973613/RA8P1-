# Titan Board RA8 Wi-Fi & AI Project Overview

This project is an RT-Thread based firmware for the Renesas RA8 series (Cortex-M85) MCU, specifically targeting the **Titan Board**. It integrates high-performance AI inference (using the Ethos-U55 NPU), camera/LCD interfacing, and Wi-Fi connectivity.

## Project Architecture

- **Operating System:** RT-Thread OS.
- **Core Hardware:**
  - **MCU:** Renesas RA8 series (Cortex-M85 with Helium and Ethos-U55 NPU).
  - **Wi-Fi:** CYW43438 module connected via SDIO.
  - **Camera:** OV5640 sensor via CEU.
  - **LCD:** ST7789 via SPI.
  - **Storage:** OSPI Flash (LittleFS) and HyperRAM.
- **AI/ML Components:**
  - Optimized models for Ethos-U55 NPU located in `src/models*`.
  - YOLO-based object detection (Face, Gesture).
  - Face recognition pipeline (MobileFaceNet).
- **Communication:**
  - LwIP stack for TCP/IP.
  - Integrated Web Server for live streaming and device control.
  - PC-side synchronization via HTTP APIs.

## Directory Structure

- `src/`: Application source code.
  - `hal_entry.c`: Main entry point and core application logic (Main Loop).
  - `models/`, `models_face/`, `models_gesture/`: NPU-optimized model implementations.
  - `yolo/`, `yolo_face/`, `yolo_gesture/`: Detection logic.
  - `face_detect.c` / `gesture_detect.c`: High-level AI pipelines.
  - `web_server.c`: HTTP server implementation.
- `board/`: Board-specific drivers and RT-Thread BSP.
- `ra/`, `ra_gen/`, `ra_cfg/`: Renesas FSP (Flexible Software Package) generated code and configurations.
- `libraries/HAL_Drivers/`: RT-Thread drivers for RA8 peripherals.
- `packages/`: RT-Thread online packages (e.g., `littlefs`, `wifi-host-driver`).
- `firmware/`: Binary firmware blobs for the Wi-Fi module.

## Building and Running

### Prerequisites
- **IDE:** RT-Thread Studio (recommended) or Keil MDK / IAR.
- **Toolchain:** `arm-none-eabi-gcc`, `armclang`, or `iccarm`.

### Build Commands
This project uses the RT-Thread SCons build system.
- To configure: `scons --menuconfig` (or use RT-Thread Studio Settings).
- To build: `scons -jN` (where N is the number of cores).
- To generate project files:
  - Keil: `scons --target=mdk5`
  - IAR: `scons --target=iar`
  - CMake: `scons --target=cmake`

### Deployment
1. Flash the compiled `rtthread.elf` or `rtthread.hex` to the board.
2. **Wi-Fi Firmware:** If Wi-Fi fails to initialize, download the firmware blobs from `/firmware` to the Flash partition using the `whd_res_download` MSH command via YMODEM.

## Development Conventions

- **RT-Thread Standards:** Follows standard RT-Thread device driver and component models (FAL, DFS, SAL, WLAN).
- **Hardware Abstraction:** Peripherals are configured via Renesas FSP (`configuration.xml`) and accessed via RT-Thread `rt_device` API or FSP `R_*` APIs for performance-critical sections (like NPU/Camera).
- **AI Integration:** Models are typically converted to C code for Ethos-U. Coordinate mapping between Camera (RGB565) and Model input (RGB888/Float) is handled in `hal_entry.c` and specialized glue code.
- **MSH Commands:** Many application features (face enrollment, database management, buzzer tests) are exposed as FinSH/MSH commands for easy debugging. Check `MSH_CMD_EXPORT` in `hal_entry.c`.

## Key Configuration Files
- `rtconfig.h`: Enabled RT-Thread components and packages.
- `rtconfig.py`: SCons build configuration (toolchain, flags).
- `configuration.xml`: FSP configuration for clocks, pins, and peripherals.
- `SConstruct` / `SConscript`: Build scripts.
