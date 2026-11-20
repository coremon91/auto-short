# Phase 0 build notes

This tree is the device-scan and record-start preflight for the XDCAM recorder. It does not capture, encode, or write MXF.

## This Linux build

The DeckLink backend is compiled only when `DeckLinkAPI.h` and `DeckLinkAPIDispatch.cpp` are in the same directory at configure time. This workspace has neither the headers nor a DeckLink PCI device (no vendor `0xbdbd` function in sysfs, no `/dev/blackmagic` node). CMake therefore leaves `src/decklink_backend.cpp` out of the build and the scan says so. That is not a hardware pass and not a DeckLink 4K Extreme result.

`scan` with no backend flag uses `auto`. When the DeckLink backend was not built, `auto` selects the mock fixture `one-input`: one full-duplex SDI I/O input, not four inputs. A later machine with the SDK headers and a 1-channel card should configure with `-DDECKLINK_SDK_DIR=` pointing at the directory that contains both `DeckLinkAPI.h` and `DeckLinkAPIDispatch.cpp`, then run `xdcam-scan scan --backend decklink`. The backend calls `IDeckLinkIterator` and the SDK 11+ attribute and status accessors named in the product spec. It probes an input with `EnableVideoInput` and `EnableAudioInput` and does not start streams. SDK 11.0 through 11.4 and SDK 11.5+ use the `DoesSupportVideoMode` signatures documented for those versions. Headers older than SDK 11 are rejected.

The mock fixture is deterministic and is never a hardware pass. `hardware_pass` stays false even if a later DeckLink scan lists a card. Phase 0 does not claim a recording run.

## Channel count

`channel_count` may be omitted. The preflight then uses the number of inputs whose simultaneous-capture probe succeeded, capped at 4. One openable input allows a 1-channel start. A request for 4 when fewer than 4 inputs can be opened together prints:

```text
4 INDEPENDENT INPUTS NOT AVAILABLE
```

and does not start a 4-channel record. That line is only the refusal of a 4-channel start. A missing second input does not block a 1-channel start, and one input is not copied into extra files.

Config values outside the XDCAM HD422 target (1920x1080, 30000/1001, interlaced, top field first, yuv422p, 50 Mbps, PCM 48 kHz 8-channel 24-bit, `STOP_WITH_ERROR` or `KEEP_RECORDING_WITH_FLAG`) are rejected at startup. Preflight also checks that the output directory exists and is writable, that free space covers `min_free_bytes`, that two channels do not name the same input, and that a selected device is not busy or unable to open 8-channel audio.

## Commands

```text
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/xdcam-scan scan
mkdir -p /tmp/xdcam-phase0
./build/xdcam-scan preflight --config config/one_channel.json
./build/xdcam-scan preflight --config config/four_channel.json
```

If `CXX` is unset, CMake selects `g++`. On this image `/usr/bin/c++` is Clang and cannot link `libstdc++`, so do not point `CXX` at that compiler unless its C++ library link is fixed. `g++` 13 is the compiler this build records.

The scan prints the compiler id and version, the CMake version, and, when the tools are on `PATH`, the first line of `ffmpeg -version`, `ffprobe -version`, and `mediainfo --Version`. FFmpeg libraries are not linked in Phase 0. DeckLink header and runtime versions are printed only by a binary that was configured with the SDK.
