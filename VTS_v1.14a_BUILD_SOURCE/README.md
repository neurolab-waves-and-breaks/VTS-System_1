# VTS v1.14a — build source

Source-only package for the experimental VTS XIAO receiver firmware with Coarse/Fine
phase selection, Permanent/Stay, USB diagnostics and BLE telemetry protocol v13.

## Requirements

- **Target:** the existing VTS receiver built around the Seeed Studio XIAO
  nRF52840 board. The build target is `xiao_ble`.
- **Build environment:** nRF Connect SDK **v3.4.1** and its matching Nordic
  toolchain, as recorded in the original project documentation. Open a terminal
  configured for that SDK, with `west`, CMake, Ninja and the Arm compiler available.
- The SDK supplies Zephyr, Nordic ESB/MPSL, Bluetooth and hardware dependencies.
  This ZIP contains the application source; it does not include the SDK.

## Build the UF2 on Windows

Extract the ZIP so that `CMakeLists.txt` is directly inside:

```text
C:\VTS_v1.14a_BUILD_SOURCE
```

In the configured nRF Connect SDK PowerShell terminal, run:

```powershell
Set-Location C:\VTS_v1.14a_BUILD_SOURCE

west build -p always -b xiao_ble -d build_ble . --extra-conf ble_telemetry.conf
if ($LASTEXITCODE -ne 0) { throw "Firmware build failed." }

Get-ChildItem -Path .\build_ble -Recurse -Filter zephyr.uf2 |
    Select-Object -ExpandProperty FullName
```

`ble_telemetry.conf` is required for the BLE-enabled configuration. It retains
impedance telemetry and enables the firmware's existing BLE advertising settings.

The last command prints the generated UF2 path. With sysbuild, it is normally:

```text
build_ble\VTS_v1.14a_BUILD_SOURCE\zephyr\zephyr.uf2
```

Depending on the build environment, it may instead be
`build_ble\zephyr\zephyr.uf2`. Use the path printed after the successful build.
If you rename the project folder, the sysbuild application directory can change.

The generated `zephyr.uf2` is the receiver firmware file to share or load onto
the VTS XIAO using the team's existing UF2 bootloader procedure.

## Included files

| Path | Purpose |
| --- | --- |
| `src/` | All 34 firmware source and header files |
| `CMakeLists.txt` | Application build definition |
| `Kconfig` | Application configuration options |
| `prj.conf` | Base receiver, storage, USB and UF2 configuration |
| `app.overlay` | Receiver hardware and USB devicetree configuration |
| `ble_telemetry.conf` | BLE-enabled configuration used by the build command |
| `README.md` | These build instructions |

## Hardware and application compatibility

- The receiver configuration uses a DA7280 on `i2c1`, address `0x4A`. The phase
  button connects XIAO **D9 / P1.14** to **3.3 V**, with an internal pull-down.
  Keep the existing VTS hardware wiring.
- The receiver radio configuration is **ESB channel 17, 1 Mbps**. This package is
  for the VTS receiver; it does not contain CLIC or USB-dongle firmware.
- Optional Android viewing requires BLE protocol **v13** support. The paired
  application is **PhaseViewerAndroid v1.13a DUAL_RESOLUTION_V13_COMPILE_FIX1**.
  Android is not needed to compile the firmware.
- The firmware release label remains `v1.14a-dual-resolution-fsm`.

No compiled UF2, development tests, bench logs or audit documents are included.
