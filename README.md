# Stage Plot Mixer

An open-source, real-time, multi-channel audio mixer and recorder for **live sound and home studio** use. It is built around a hierarchical node-and-wire graph, so the signal flow on screen is exactly what you hear.

> **Status: Phase 0 (foundations & hardware check).** The mixer itself isn't usable yet. The current deliverable is `spm-diag`, a hardware diagnostics tool. See [docs/TESTING.md](docs/TESTING.md).

## Planned features

- Node-based routing with splitting, merging, effects loops and hardware inserts, all time-aligned automatically
- Hierarchical groups and templates; tear-off control panels with names, icons and layouts
- Meters: linear / dBFS / VU scales; vertical, horizontal, circular and analog styles
- Records raw and processed signals from any point in the graph (Broadcast WAV / RF64)
- ASIO (USB & FireWire interfaces) and WASAPI on Windows 10/11; captures audio from other applications
- VST3 and LADSPA plugins
- Windows first, with macOS and Linux to follow

Full design: [docs/DESIGN.md](docs/DESIGN.md)

## Building

Requirements: CMake ≥ 3.22 and a C++20 compiler (Visual Studio 2022, Xcode/Clang 15+, or GCC 11+). Dependencies (JUCE 9, Catch2) are downloaded automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

On Linux, first install `libasound2-dev`, `libjack-jackd2-dev`, `libfreetype-dev` and `libfontconfig1-dev`.

## Layout

| Path | Contents |
|---|---|
| `src/core` | Framework-independent building blocks (real-time statistics, latency measurement) |
| `src/platform` | OS-specific code behind portable interfaces |
| `tools/spm-diag` | Hardware diagnostics tool |
| `tests` | Unit tests |
| `docs` | Design document and testing guide |

## License

[GNU AGPLv3](LICENSE). Stage Plot Mixer uses JUCE (AGPLv3) and the Steinberg ASIO SDK (GPLv3 option). ASIO is a trademark of Steinberg Media Technologies GmbH.
