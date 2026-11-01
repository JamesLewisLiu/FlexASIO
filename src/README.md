# FlexASIO Developer Information

[![.github/workflows/continuous-integration.yml](https://github.com/dechamps/FlexASIO/actions/workflows/continuous-integration.yml/badge.svg)](https://github.com/dechamps/FlexASIO/actions/workflows/continuous-integration.yml)

See `LICENSE.txt` for licensing information. In particular, do note that
specific license terms apply to the ASIO trademark and ASIO SDK.

## Building

FlexASIO is designed to be built using CMake within the Microsoft Visual C++
2019/2022 toolchain native CMake support.

FlexASIO uses a CMake "superbuild" system (in `/src`) to automatically build the
dependencies (most notably [PortAudio][]) before building FlexASIO itself. These
dependencies are pulled in as git submodules.

It is strongly recommended to use the superbuild system. Providing dependencies
manually is quite tedious because FlexASIO uses a highly modular structure that
relies on many small subprojects.

Note that the ASIOUtil build system will download the [ASIO SDK][] for you
automatically at configure time.

## Mixer tests

The FlexASIO subproject builds `FlexASIOMixerTest` and `FlexASIODriverTest` when
`BUILD_TESTING` is enabled (the default). After a superbuild, run CTest in
`<build>/FlexASIO-prefix/src/FlexASIO-build` for the device-independent mixer and
TOML tests. These use explicit checks in Release builds as well as Debug builds.

The driver test is manual because it requires a working WASAPI playback device.
Put `<build>/install/bin` on PATH, then run:

```
FlexASIODriverTest.exe <scratch-directory> "Speakers (Realtek(R) Audio)"
```

`--low-latency` sets `suggestedLatencySeconds = 0.0`. Additional options:

- `--frames N`: set the ASIO buffer size (32 to 4096 frames, default 512).
- `--exclusive`: open the selected WASAPI output exclusively.
- `--convert`: allow PortAudio to negotiate the hardware sample format while
  retaining Int32 at the ASIO interface.
- `--seconds N`: keep each of the two start/stop cycles running for N additional
  seconds (0 to 60, default 0).
- `--profile`: run only the enabled-mixer OutputReady profile.

For example, to exercise the low-latency digital output profile:

```
FlexASIODriverTest.exe <scratch-directory> "Realtek Digital Output (Realtek(R) Audio)" --low-latency --exclusive --convert --frames 128 --profile --seconds 15
```

The test plays silence, uses its scratch directory for configuration (never the
live user configuration), verifies channel enumeration, 48 kHz / selected-buffer /
Int32 operation, both callback forms, OutputReady, restart, configuration reset,
invalid hardware settings, and the disabled-mixer path. It does not validate
audible game channel assignments. On systems using a non-UTF-8 source code page,
set the MSVC `CL` environment variable to `/utf-8` when building dependencies.

## Packaging

The following command will generate the installer package for you:

```
cmake -P installer.cmake
```

Note that for this command to work:

 -  You need to have [Inno Setup][] installed.
 -  You need to have built FlexASIO in the `x64-Release` and `x86-Release`
    Visual Studio configurations first.

**Note:** instead of running `installer.cmake` manually, it is often preferable
to let the FlexASIO GitHub Actions workflow build the installer, as that
guarantees a clean build.

## Troubleshooting

### VC runtime DLLs are not included in the installation

See this [Visual Studio 2019 bug][InstallRequiredSystemLibraries].

---

*ASIO is a trademark and software of Steinberg Media Technologies GmbH*

[ASIO SDK]: http://www.steinberg.net/en/company/developer.html
[Inno Setup]: http://www.jrsoftware.org/isdl.php
[InstallRequiredSystemLibraries]: https://developercommunity.visualstudio.com/content/problem/618084/cmake-installrequiredsystemlibraries-broken-in-lat.html
[PortAudio]: http://www.portaudio.com/
[tinytoml]: https://github.com/mayah/tinytoml
