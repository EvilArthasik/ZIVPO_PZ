# Windows installer

Inno Setup-based installer for `TraySampleApp.exe` (tray GUI) and
`TraySampleService.exe` (Windows service).

## Mapping to functional requirements

| # | Requirement | Implementation |
|---|---|---|
| 1 | Bundle all executables, DLLs, resources, config files | `scripts/build-windows-installer.ps1` stages `TraySampleApp.exe` and `TraySampleService.exe` into `build\installer\package\`; `TraySampleApp.iss` `[Files]` copies the whole `package\*` into `{app}`. The project produces no DLLs or external resource files (single-EXE per binary). |
| 2 | Install third-party runtime dependencies | The tray app and service are linked against the **static** MSVC runtime (`CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded"` in `CMakeLists.txt:5`), so no VC++ Redistributable is required. The project does not depend on Qt, Windows App SDK, or .NET. All Win32 / RPC APIs used are part of Windows itself, so there is nothing additional to ship or to clean up on uninstall. |
| 3 | Register Windows service with auto-start | `TraySampleApp.iss` `[Run]` calls `TraySampleService.exe /install`, which executes `CreateService` with `SERVICE_AUTO_START` (`src/service/service_main.cpp:734`). The service starts immediately via `sc start TraySampleService`. |
| 4 | Remove all files on uninstall | `[UninstallDelete] Type: filesandordirs; Name: "{app}"` wipes the entire install directory. |
| 5 | Remove dependencies if no longer used | Nothing system-wide is installed (everything sits inside `{app}`), so the `{app}` wipe is exhaustive. |
| 6 | Stop and remove the service on uninstall | `[UninstallRun]` runs `sc stop TraySampleService` followed by `TraySampleService.exe /uninstall`, which calls `DeleteService` (`src/service/service_main.cpp:792`). |
| 7 | Build on CI and publish as build artifact | `.github/workflows/build.yml` job `windows-installer` installs Inno Setup, runs `scripts/build-windows-installer.ps1`, and uploads `TraySampleApp-Setup-*.exe` as the `TraySampleApp-windows-installer` artifact. |

## Local build (Windows)

Prerequisites:
- Visual Studio 2022 Build Tools with the C++ workload and CMake.
- Inno Setup 6 (`choco install innosetup -y`).

```powershell
.\scripts\build-windows-installer.ps1 -AppVersion 0.0.1
```

The resulting installer is written to
`build\installer\output\TraySampleApp-Setup-<version>.exe`.

Useful switches:
- `-SkipBuild` — reuse an existing `build\Release\*.exe` instead of running CMake again.
- `-Configuration Debug` — package a debug build (not recommended for distribution).
- `-InnoSetupCompiler "C:\path\to\ISCC.exe"` — explicit path to ISCC when it is not in the default locations or PATH.

## Installation behavior

Running the installer (admin rights are required):

1. Installs files into `Program Files\TraySampleApp`.
2. Best-effort stop and unregister of any previously installed service version.
3. Calls `TraySampleService.exe /install` — service is registered with
   `SERVICE_AUTO_START`, so it will also start on every subsequent boot.
4. Calls `sc start TraySampleService` — service starts immediately, no reboot
   needed.

## Uninstall behavior

Triggered from Settings → Apps, Control Panel, or
`"C:\Program Files\TraySampleApp\unins000.exe"`:

1. `sc stop TraySampleService` — graceful stop.
2. `TraySampleService.exe /uninstall` — `DeleteService` removes it from SCM.
3. The entire `{app}` directory is removed.

After uninstall, `Get-Service TraySampleService` should report
*"Cannot find any service with service name 'TraySampleService'"*.

## CI build

The `windows-installer` job in `.github/workflows/build.yml`:

1. Checks out the repository on `windows-latest`.
2. Installs Inno Setup via `choco install innosetup -y --no-progress`.
3. Runs `scripts/build-windows-installer.ps1`, which performs the CMake
   configure + Release build and the Inno Setup compilation.
4. Uploads `build/installer/output/TraySampleApp-Setup-*.exe` as the
   `TraySampleApp-windows-installer` artifact (downloadable from the workflow
   run page).
