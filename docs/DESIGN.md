# Stage Plot Mixer — Design Document

*A multi-channel real-time audio mixer & recorder*

**Status:** v0.6 (requirements confirmed; Phase 2 in testing)
**Target platforms:** Windows 10 / 11 (x64) first; macOS and Linux later
**License:** GNU AGPLv3 (see [§3.5](#35-licensing-open-source))
**Repository:** <https://github.com/dl33ds/stage-plot-mixer>
**Last updated:** 2026-09-24

---

## 1. Purpose & Scope

A desktop application for **live sound and home studio** use that:

- Takes audio from one multi-channel audio interface (FireWire, USB or built-in Windows audio)
- Also takes audio **played by other applications on the same computer** (media players, backing tracks, browsers)
- Routes, processes and mixes that audio in real time, with low latency
- Is built around a **hierarchical node-and-wire graph** with a clean, modern, flat UI
- Hosts third-party **VST3 and LADSPA** effect plugins
- Shows signals on configurable level meters
- **Records** raw and/or processed signals to disk

### 1.1 Confirmed requirements

| # | Requirement | Decision |
|---|---|---|
| R1 | Tear-off graphic units with names/icons, placeable & sizeable individually and in groups | Node graph + tear-off control panels (§5.1, §5.2) |
| R2 | Metering: linear, logarithmic, vertical, horizontal, circular, analog | §5.3 |
| R3 | Record raw and processed signals | Stream to disk, BWF/RF64 (§5.4) |
| R4 | Flexible routing: effects loops, splitting, merging | Hierarchical node graph (§5.1) |
| R5 | USB and FireWire interfaces. **FireWire is mandatory** | Via ASIO drivers supplied by the owner (§3.3) |
| R6 | Future deployment on other OSes | JUCE + isolated platform layer (§5.9) |
| R7 | **Single ASIO device at a time** (default) | §3.2 |
| R8 | 32 channels @ 48 kHz | Sizing target (§7) |
| R9 | **Record only**, no playback/editing | Recording (§5.4). Audio from other apps as an input is in the backlog (§14) |
| R10 | Third-party plugins: **VST3 and LADSPA** | §5.7 (VST2 not needed) |
| R11 | Clean, modern, flat, intuitive; **legible font** | §5.2 (UI), font: Inter |
| R12 | Capture and record from **multiple points** in the signal paths | Any number of Recorder nodes, anywhere in the graph (§5.4) |
| R13 | No traditional mixing-console view | The node graph and user-built panels are the only views |

### 1.2 Out of scope (v1)
- Playback of recorded takes, overdubbing, timeline/waveform editing (this is **not a DAW**)
- Using several hardware interfaces at the same time (clock aggregation)
- An automatically generated traditional mixing-console view (not wanted)
- VST2 plugins
- MIDI sequencing; network audio (Dante/AES67/NDI); mobile

---

## 2. Glossary

| Term | Meaning |
|---|---|
| **Buffer / block** | A group of samples handed over by the driver on each callback (e.g. 128 samples) |
| **Latency** | Delay from input to output. 128 samples @ 48 kHz ≈ 2.7 ms per buffer, in each direction |
| **Node** | A box in the graph: an input, effect, bus, output, recorder, meter, etc. |
| **Port** | A connection point on a node (input on the left, output on the right) |
| **Wire** | A connection from an output port to an input port |
| **Group node** | A node that contains its own sub-graph (hierarchy) |
| **Face** | A node's controls (fader, meter, knobs), which can be torn off into a panel |
| **Tap point** | A place in the graph where a signal can be metered or recorded |
| **Bus** | A node that sums (merges) several signals |
| **Send / Return** | A copy of a signal sent to an effect, and that effect's output coming back |
| **PDC** | Path Delay Compensation: keeps parallel paths time-aligned |
| **ASRC** | Asynchronous Sample-Rate Conversion: adapts audio running on a different clock |
| **ADAT** | Optical digital format carrying 8 channels @ 48 kHz per cable |

---

## 3. Key Technical Decisions

### 3.1 Language & framework — **C++20 + JUCE 9**

JUCE is the industry-standard C++ audio framework. It provides:
- Audio device I/O: ASIO, WASAPI (Windows); CoreAudio (macOS); ALSA, JACK (Linux)
- A GPU-accelerated custom-drawn GUI, suited to a node editor and meters
- WAV/AIFF/FLAC writers, and a BWF metadata API
- VST3 plugin hosting (LADSPA is built in on Linux only; we extend it, see §5.7)
- The ASIO 2.3 headers (Steinberg), bundled, so ASIO needs only a build flag
- One codebase for Windows, macOS and Linux (R6)

**Build tooling:** CMake, MSVC (Visual Studio 2022 Build Tools) on Windows, Clang on macOS/Linux. GitHub Actions CI builds all three platforms from the start.

### 3.2 Audio device model

- **One primary device at a time** (R7). Its driver type is either:
  - **ASIO** (default): FireWire 1814, ProFire Lightbridge, Scarlett Solo
  - **WASAPI**: Windows system audio devices (built-in sound, HDMI, Bluetooth, etc.)
- The primary device sets the **master clock** and sample rate (48 kHz target).

### 3.3 Target hardware

| Device | Connection | Driver (Windows) | I/O @ 48 kHz (verify in Phase 0) | Notes |
|---|---|---|---|---|
| **M-Audio FireWire 1814** | FireWire 400 | M-Audio FireWire ASIO (supplied by owner) | up to 18 in / 14 out (8 analog + 8 ADAT + 2 S/PDIF in) | Mic preamps on inputs 1–2 |
| **M-Audio ProFire Lightbridge** | FireWire 400 | M-Audio ProFire ASIO (supplied by owner) | ~34 in / 36 out (4× ADAT = 32 ch + S/PDIF) | The main **32-channel** device; inputs come from external ADAT preamps/converters |
| **Focusrite Scarlett Solo 3rd Gen** | USB 2.0 | Focusrite USB ASIO | 2 in / 2 out | Good low-channel test device |
| **Windows system audio** | Built-in/USB/HDMI/BT | WASAPI (shared/exclusive) | varies | Higher latency; fine for practice/playback |

**FireWire notes:**
- The app only talks to the **ASIO driver**, never to FireWire directly. If the vendor driver works in Windows, the app works.
- The PC needs a FireWire (IEEE 1394) PCIe card. **Texas Instruments chipset** cards are by far the most reliable for audio. Some older drivers also need Microsoft's *legacy 1394 host controller driver* instead of the default Windows 10/11 one. Phase 0 checks this on the actual PC.
- The Lightbridge uses ADAT, which gives 8 channels per cable at 48 kHz. At 96 kHz the channel count halves, which is another reason to target 48 kHz.
- Future macOS port: **macOS 26 removed FireWire support**, so these two devices will not work on current macOS. Linux support depends on the FFADO project's support for each device (checked in Phase 8).

### 3.4 Windows versions
Windows 10 (22H2) and Windows 11 (23H2+), x64.

### 3.5 Licensing (open source)

| Component | License | Consequence |
|---|---|---|
| JUCE 9.0.2 (open-source option) | AGPLv3 | **The app is released under AGPLv3** |
| Steinberg ASIO SDK 2.3.4 (Oct 2025) | Dual: proprietary / GPLv3 (confirmed in the SDK's LICENSE.txt) | Use the GPLv3 option; GPLv3 code may be combined with AGPLv3. The ASIO name/logo is optional; if used, it must follow Steinberg's usage guidelines |
| Steinberg VST3 SDK (3.8+) | MIT | No conflict |
| LADSPA header | LGPL 2.1 | No conflict |
| Inter font | SIL Open Font License | May be bundled |
| Lucide icons | ISC | May be bundled |

**Licence: AGPLv3 (accepted).** VST3 SDK terms are re-checked when plugin hosting starts (Phase 6).

---

## 4. System Architecture

```
┌──────────────────────────────── UI (message thread) ─────────────────────────────────┐
│  Graph Editor (nodes & wires, hierarchy) │ Tear-off Panels (faces) │ Meters │ Recorder │
└───────────────▲─────────────────────────────────▲─────────────────────┬──────────────┘
                │ meter data (lock-free)          │ model change events │ commands (undoable)
┌───────────────┴───────────┐        ┌────────────┴─────────────────────▼──────────────┐
│   Metering Service        │        │   Session Model (single source of truth)        │
└───────────────▲───────────┘        │   graph tree, panels, undo/redo, JSON save      │
                │                    └────────────────────────┬────────────────────────┘
                │                                             │ compiled graph (atomic swap)
┌───────────────┴─────────────────────────────────────────────▼────────────────────────┐
│                         AUDIO ENGINE (real-time thread, driven by ASIO/WASAPI)        │
│  Device In ─► Flattened Graph (inputs, FX, plugins, buses, taps) ─► Device Out         │
└──────────────┬────────────────────────────────────────────┬───────────────────────────┘
               │ lock-free ring buffers                     │ plugin I/O (shared memory)
      ┌────────▼─────────┐                        ┌──────────▼──────────┐
      │  Disk Writer     │                        │ Plugin Host process │ (optional sandbox,
      │  (BWF/RF64)      │                        │ (VST3 / LADSPA)     │  §5.7)
      └──────────────────┘                        └─────────────────────┘
```

### 4.1 Threads

| Thread | Job | Rules |
|---|---|---|
| **Audio (real-time)** | Driver callback: runs the compiled graph, computes meters, pushes record data | **No** allocation, locks, file I/O, logging or UI calls |
| **Message / UI** | Drawing, input, model edits | Never blocks the audio thread |
| **Disk writer** | Drains record ring buffers to files | Absorbs several seconds of disk stall |
| **Device manager** | Hot-plug, device reopen, sample-rate changes | |
| **Workers** | Graph compilation, plugin scanning, file finalisation | |

### 4.2 Real-time safety principles
1. The UI edits the **Session Model**, never the live graph. A new compiled graph is built in the background and **swapped in atomically**. Wiring changes during a live show do not cause dropouts.
2. Parameter changes go through atomics/lock-free queues and are **smoothed** so they don't click. Connecting or removing a wire uses a short crossfade (~10 ms) instead of a hard cut.
3. All buffers are preallocated.
4. A watchdog reports CPU load, xruns (dropouts) and per-node processing time.

### 4.3 Module layout
```
/app            – application shell, windows, commands
/engine         – device I/O, compiled-graph runtime, parameter smoothing, ASRC
/graph          – graph model, hierarchy, validation, compiler (flatten, sort, PDC)
/nodes          – built-in node types (I/O, bus, send, split, gain, EQ, dynamics…)
/plugins        – VST3 + LADSPA hosting, scanning, sandbox process
/recording      – tap buffers, disk writer, BWF/RF64, takes, recovery
/metering       – meter maths, ballistics, lock-free transport
/ui             – design system, graph editor, panels/tear-off, meter views
/model          – session tree, undo/redo, JSON serialization, migrations
/platform       – OS-specific code (kept small, behind interfaces)
/tools          – hardware diagnostics utility (Phase 0)
/tests          – unit, DSP golden-file, RT-safety, soak tests
```

---

## 5. Feature Design

### 5.1 Hierarchical node graph (R1, R4)

The graph is the core of the app. **What you see in the editor is the signal flow.**

#### Nodes and ports
- A node is a rounded rectangle with a **name, icon, colour**, input ports on the left and output ports on the right. Signal flows **left to right**.
- **Ports can carry more than one channel:** mono, stereo, or N-channel bundles (e.g. "ADAT 1–8"). A bundle can be **expanded** into individual channel ports or kept collapsed. This keeps a 32-channel session readable instead of 32 separate wires.
- Every port shows its channel count, and a small **activity dot** lights up when signal is present.

#### Wires
- Smooth curved wires. Colour shows channel width (mono / stereo / multi). Selected wires are highlighted.
- **Signal-aware:** a wire glows faintly in proportion to its level, so you can see at a glance where audio is flowing. (This can be turned off.)
- To create a wire, drag from an output port to an input port. Valid targets light up and invalid ones grey out. Hovering an invalid target shows the reason (e.g. *"This would create a feedback loop"*).
- **Split:** drag several wires from one output (free; the buffer is shared).
- **Merge:** several wires into one input are **summed automatically**, and each wire has its own gain (double-click the wire). A Bus node can be used for an explicitly named mix.
- If the channel counts don't match (e.g. mono → stereo), a sensible default is applied (mono duplicates to both sides) and shown with a small badge on the wire.

#### Hierarchy
- **Group nodes** contain a sub-graph. You **double-click to enter** a group, and a **breadcrumb bar** shows where you are (`Session › Drums › Kick`). The group's exposed ports appear as input/output pins on its boundary.
- Select any nodes and choose **Group** (Ctrl+G) to make a group; the wires crossing the boundary become exposed ports automatically.
- **Templates:** a group can be saved as a reusable template. The built-in **Channel Strip** is itself a template group (Trim → Inserts → EQ → Dynamics → Pan → Fader → Mute/Solo), so it can be opened and modified like any other group.
- The audio engine **flattens** the hierarchy when compiling, so groups cost no extra processing.

#### Node library (v1)

| Category | Nodes |
|---|---|
| **Sources** | Hardware Input (device channels), Test Generator (tone, pink noise, latency ping) |
| **Destinations** | Hardware Output, Recorder (§5.4) |
| **Mixing** | Bus (N-in → 1 mix), Send (pre/post fader), Pan/Balance, Fader, Mute/Solo, Channel Strip (template) |
| **Routing** | Splitter/Router, Channel Pick (take channels 3–4 from a bundle), Bundle/Unbundle, **External Insert** (outboard gear loop with latency measurement) |
| **Processing** | Gain/Trim, Polarity, HPF/LPF, 4-band Parametric EQ, Compressor, Limiter, Gate, Delay, Reverb |
| **Plugins** | VST3 plugin, LADSPA plugin (§5.7) |
| **Analysis** | Meter, Loudness (LUFS), Spectrum (later) |

#### Graph rules
- **Effects loops:** Send → effect → Return into a bus, internal or through outboard gear (External Insert).
- **Cycles are blocked** (feedback loops). A deliberate Feedback node may be added later.
- **PDC:** the compiler delays shorter parallel paths so all paths stay sample-aligned. Nodes that add latency show a small latency badge.
- **Solo** follows the graph: soloing a node silences parallel sources feeding the same destinations.

#### Editor ergonomics
- Pan (space-drag or middle mouse), zoom (Ctrl+wheel), **minimap**, "fit to view"
- **Quick-add:** press Tab or double-click the empty canvas to get a search box for adding nodes
- Snap-to-grid, auto-align, box select, copy/paste (wires between the copied nodes are kept)
- Full **undo/redo** for every edit
- **Show Lock** (live-sound mode): wiring and layout are locked, and only faces (faders, mutes) can be adjusted. This prevents accidental changes during a performance.

### 5.2 Tear-off panels and UI design (R1, R11)

The graph handles **wiring**. The **Panels** handle **operating** the mix.

- Every node has a **Face**: its controls (fader, mute, meter, knobs, record-arm). You can drag a face out of the node (or right-click → *Tear off*) to:
  - a **floating window** (any monitor), or
  - a **Panel**: a free-form surface you arrange like a custom mixing console
- **Faces stay linked** to their nodes. Moving a torn-off fader moves the node's fader.
- **Placement & sizing:** free positioning with snap-to-grid/edges; each face can be resized. Faces come in compact / standard / large sizes, plus free resize.
- **Grouping:** select several faces to move/resize them together, or make a **named face group** ("Drums", "Vocals") that moves, collapses and tears off as one unit. **Control links** (e.g. linked faders) are separate from layout groups.
- **Layouts:** named snapshots ("Soundcheck", "Show", "Studio") of panels and window positions, switchable instantly and saved with the session.
- Positions are restored per monitor, and windows are brought back on screen if a monitor is missing.

#### Visual design system
- **Style:** flat, minimal, no skeuomorphism (except the optional analog meter face). Consistent 4-px spacing grid; 6-px corner radius; one accent colour.
- **Theme:** **dark by default** (for stage and venue lighting), with a light theme option. Contrast meets WCAG AA (≥ 4.5:1 for text).
- **Font: Inter** (bundled, SIL OFL). It was designed for screen UI and is very legible at small sizes.
  - **Tabular (fixed-width) numbers** for every changing number (dB values, timecode, meters), so digits don't jitter as values change.
  - Base size 13 px at 100% scale; minimum 11 px; headings 15–18 px.
  - The font is bundled with the app rather than relying on system fonts, so text looks the same on every OS.
- **Icons: Lucide** (bundled, ISC). Includes mic, guitar, drum, piano, speaker, headphones, radio and more. Users can import their own SVG/PNG icons.
- **Colour meaning is consistent:** red = recording/clip, amber = warning, green = signal present, accent = selected. Colour is never the only indicator (shapes/labels too).
- **Intuitive by default:** tooltips on everything, plain-language labels ("Record", not "Arm TX"), a first-run guided setup (choose device → name inputs → create starter graph), and empty states that explain what to do next.
- **UI scaling:** per-monitor DPI 100–300%, vector-drawn throughout.
- **Keyboard:** all common actions have shortcuts; a searchable command palette (Ctrl+K).

### 5.3 Metering (R2)

Metering is split into **measurement** (on the audio thread) and **display** (on the UI thread), so any display style can show any measurement.

**Measurements** (per channel, any tap point): sample peak, RMS (window can be set), peak-hold, latching clip indicator. Optional: true-peak, LUFS (momentary/short-term/integrated), stereo correlation.

**Scales:** Linear (0–100%), Logarithmic dBFS (range can be set, e.g. −60…0), VU (−20…+3, reference can be set, default 0 VU = −18 dBFS).

**Ballistics:** Digital peak, VU (300 ms), PPM (fast attack / slow release), Custom.

**Styles:**

| Style | Use |
|---|---|
| Vertical bar | Channel faces, meter bridge |
| Horizontal bar | Compact faces, inside nodes |
| Circular / radial | Arc or ring; dashboard panels |
| Analog needle | VU/PPM look with needle physics (mass/damping) |
| Numeric | Peak/RMS readout (tabular numbers) |

Each meter can be set to its own style, scale, ballistics, colour zones and size. Meter settings can be saved as presets. Meters render at 60 fps, and only regions that changed are redrawn.

**Transport:** the audio thread records a running peak/RMS maximum for each channel in lock-free slots. The UI reads and resets them each frame, so the audio thread never waits.

### 5.4 Recording (R3)

#### What is recorded
A **Recorder node** can be wired to *any* point in the graph. Typical setups:
- **Raw:** wired straight from Hardware Input (safety tracks)
- **Processed:** wired after the channel strip / effects
- **Mix:** wired from the master bus
- Several Recorder nodes can run at once. **Raw and processed from the same source is simply two wires.**
- A global **Record** button starts/stops every armed Recorder together, sample-aligned.

#### Memory vs. disk — **Decision: stream to disk through RAM ring buffers**
- The audio thread copies samples into a per-channel lock-free ring buffer (~10 s deep).
- The disk-writer thread drains the buffers in large sequential writes.
- **Pre-roll (optional):** always keep the last 30–120 s of armed inputs in RAM. When Record is pressed, that audio is written first, so a moment that has already happened is not lost. (≈ 23 MB per minute for 32 ch @ 48 kHz/24-bit.)
- If a buffer overflows, a dropout marker is logged. The audio never stops.

**Data rates at 48 kHz, 32 channels:**

| Format | Rate | Per hour | Raw + processed |
|---|---|---|---|
| 24-bit PCM | 4.6 MB/s | ~16.6 GB | ~33 GB/h |
| 32-bit float | 6.1 MB/s | ~22 GB | ~44 GB/h |

An SSD is recommended. The Recorder shows **remaining record time** and warns at a threshold you can set.

#### File format
- **Broadcast WAV (BWF)**, with automatic **RF64** for files that would exceed 4 GB
- Sample format: **24-bit PCM default for raw inputs**; **32-bit float default for processed/mix** (cannot clip). Each Recorder can change this.
- **One mono file per channel** (stereo buses as interleaved stereo). Every DAW imports this, and each file stays under 4 GB for ~7 hours at 48 kHz/24-bit.
- Metadata in BWF + a `take.json`: node/channel names, tap point, device, sample rate, start time, markers, dropouts
- **Markers:** press M during recording to drop a named marker (written to `take.json` and as BWF cue points)
- **Crash safety:** headers are rewritten every ~2 s; unfinished takes are detected and repaired on next launch

```
MySession/
  MySession.mixproj
  Takes/
    2026-09-24_1930_Take03/
      Kick_raw.wav   Kick_post.wav   Snare_raw.wav ...
      MainMix.wav
      take.json
```

### 5.5 Devices (R5, R7)
- **Audio Settings:** driver type (ASIO/WASAPI) → device → sample rate → buffer size; shows input/output/round-trip latency
- The ASIO driver's control panel can be opened from the app
- **Channel naming** per device (e.g. ADAT 1 → "Kick In"), saved and reused
- **Hot-plug / dropout recovery:** if the device disappears (FireWire cable bump), the engine stops cleanly, the graph shows the inputs as *offline*, any active recording is closed safely, and the device reconnects automatically when it returns
- **Device remap:** opening a session made on another interface prompts to map old channels to new ones
- Status bar: device, sample rate, buffer, CPU load, xrun count, disk space

### 5.6 Audio from other applications

Not planned for now. The design is kept in the backlog (§14.1).

### 5.7 Plugins (R10)

| Format | Plan |
|---|---|
| **VST3** ✅ | JUCE's host. Plugin editors open in floating windows (which work like tear-offs). Parameters can be shown as a generic face. |
| **LADSPA** ✅ | JUCE's LADSPA host is Linux-only, so we write a **small cross-platform LADSPA host** (the API is one simple C header). LADSPA plugins have no GUI, so we **generate a face automatically** from the plugin's port descriptions (knobs/sliders/toggles). |
| VST2 | **Not supported** (VST3 is enough; VST2 is no longer licensed by Steinberg). |
| LV2, CLAP | Open formats; could be added later (LV2 is LADSPA's successor). |

- **Plugin scanner** runs in a separate process, so a crashing plugin can't take down the app. It keeps a list of known-bad plugins.
- **Live-sound safety (strongly recommended):** option to run plugins in a **sandbox process**. If a plugin crashes, the audio continues: that node passes audio through unprocessed or goes silent (user's choice), and recording carries on. This adds one buffer of latency to sandboxed plugins, so it is set per plugin.
- Plugin state is saved in the session; latency reported by a plugin feeds PDC.

### 5.8 Live-sound safety features
- **Safe start:** outputs start muted until the user unmutes (prevents a loud burst of noise or feedback at launch)
- **Output protection limiter** on hardware outputs (optional, per output)
- **Show Lock** (§5.1)
- No pop-up dialogs that block the mix; warnings appear as non-blocking banners
- The engine keeps running while settings screens are open, except when changing device or sample rate

### 5.9 Cross-platform (R6)
- Everything goes through JUCE abstractions; OS-specific code lives in `/platform` (FireWire notes, file dialogs if needed)
- Session files are platform-neutral (UTF-8 JSON, relative paths)
- CI builds Windows, macOS and Linux from Phase 1
- macOS: CoreAudio; signing + notarisation; **no FireWire**
- Linux: ALSA, JACK/PipeWire; FireWire via FFADO (per device)

---

## 6. Data Model & Persistence
- **Session** = folder with `*.mixproj` (JSON), `Takes/`, `Assets/` (custom icons)
- Stores: device config & channel names, full graph hierarchy, node parameters & plugin state, faces/panels/layouts, meter settings, take list
- A **schema version** with migrations, so old sessions always open
- **Undo/redo** for every edit, including layout
- **Autosave** + crash recovery
- **Presets/templates:** group templates (channel strips, FX racks), plugin presets, meter presets, layout templates, session templates ("8-ch band", "32-ch live")

---

## 7. Non-Functional Targets

| Area | Target |
|---|---|
| Channels | 32 in / 32 out @ 48 kHz (ProFire Lightbridge) |
| Latency | No added latency beyond the driver buffer, except sandboxed plugins and PDC; 64–128-sample buffers usable |
| Stability | 8-hour soak test: 32 ch recording raw + processed, 128 samples, zero xruns |
| CPU | < 25% of one core for 32-ch pass-through + metering (excluding plugins) |
| UI | 60 fps with 64 meters visible and a 200-node graph |
| Crash safety | ≤ ~2 s of audio lost on hard power loss |
| Accessibility | WCAG AA contrast, keyboard navigation, UI scaling, colour-blind-safe meter palettes |

---

## 8. Development & Test Workflow

- **Development** happens on macOS, where the engine, graph and UI can be built and tested with CoreAudio.
- **Windows builds** are produced by CI (GitHub Actions Windows runners) for every change.
- **Hardware testing** on the Windows PC is done by the owner. **Currently available: Focusrite Scarlett Solo 3rd Gen + Windows system audio.** The FireWire 1814 and ProFire Lightbridge will be available later. Until then, 32-channel behaviour is tested with the fake-device harness, and FireWire validation is a gate before the first release (see §9).
- Results come back as **GitHub issues** with the report files attached (see [TESTING.md](TESTING.md)). To make this practical remotely:
  - a **diagnostics tool** (Phase 0) lists ASIO drivers, channels, supported rates/buffers, and measures round-trip latency with a loopback cable
  - the app has **"Export diagnostics"** (logs, device report, session, xrun history) to send back for debugging
- **Automated tests:** unit tests, DSP golden files, an offline engine with a fake device (runs in CI), an RT-safety checker (flags allocations/locks on the audio thread), and file-integrity checks on recordings.

---

## 9. Production Phases

Each phase ends with a working, demonstrable build.

| Phase | Name | Deliverables | Exit criteria |
|---|---|---|---|
| **0** | Foundations & hardware check | Repo, CMake, CI (Win/Mac/Linux), licence confirmation; **`spm-diag` tool**: enumerate ASIO/WASAPI devices, channel names, rates, buffers; stability test; loopback latency test; FireWire card/driver report | Scarlett Solo (ASIO) and system audio (WASAPI) pass stability and latency tests at 48 kHz |
| **0b** | FireWire validation *(when hardware arrives; runs alongside later phases)* | `spm-diag` runs on the FireWire 1814 and ProFire Lightbridge | Both enumerate and pass at 48 kHz; Lightbridge shows 32+ inputs; **1-hour 32-ch pass-through with no xruns at 128 samples** (moved here from Phase 1). **Required before the first release** |
| **1** | Engine core | Device manager (ASIO/WASAPI), compiled-graph runtime with a fixed test graph, parameter smoothing, CPU/xrun monitor, basic meters, safe start | 1-hour 32-ch pass-through on the **simulated device** with bit-exact output while the graph is edited; **10-minute run on the Scarlett (ASIO, 128 samples)** with no xruns. The 1-hour 32-ch hardware run is in Phase 0b |
| **2** | Graph editor v1 | Design system (Inter, theme, icons), node canvas (pan/zoom/minimap), ports/bundles, wires, create/delete/connect, validation, undo/redo, save/load, atomic graph swap, built-in mixing/routing nodes | A 32-ch live mix (simulated device; Scarlett for audible checks) can be wired and changed during playback without clicks |
| **3** | Recording | Recorder node, ring buffers, disk writer, BWF/RF64, mono files, take management, markers, pre-roll, disk-time display, crash recovery | 8-h soak test, 32 ch raw + processed, bit-exact raw files |
| **4** | Hierarchy & faces | Group nodes, breadcrumb navigation, templates, channel-strip template, faces, tear-off windows, panels, face groups, layouts, Show Lock | A full "Show" layout survives save/restore and monitor changes |
| **5** | Metering suite | All styles/scales/ballistics, peak-hold/clip, meter presets, LUFS/true-peak | All meter types configurable; 64 meters at 60 fps within budget |
| **6** | Processing & plugins | Built-in effects, External Insert with latency ping, PDC, VST3 hosting, cross-platform LADSPA host with auto-generated faces, plugin scanner, **plugin sandbox** | Parallel paths stay sample-aligned; a crashing plugin doesn't stop audio or recording |
| **7** | Polish & release (Windows) | First-run setup, tooltips/help, command palette, keyboard shortcuts, light theme, installer (Inno Setup or MSIX), code signing, crash reporter, user guide, hardware compatibility list | Clean install and a full live/studio session on the target PC |
| **8** | Cross-platform | macOS build (signing/notarisation), Linux build (ALSA/JACK/PipeWire, FFADO check) | Feature parity where hardware allows |
| **Later** | Extensions | Multi-device aggregation, VST2 (if cleared), LV2/CLAP, MIDI control surfaces, FLAC recording, spectrum analyzer, remote control; see also the backlog (§14) | — |

Phase 5 is largely independent of 3–4 and can move earlier if needed.

---

## 10. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Legacy M-Audio FireWire drivers on Win 10/11 | Main 32-ch device may be unstable | Phase 0 check on target PC; TI-chipset card; legacy 1394 driver if needed; Scarlett/WASAPI as fallback |
| No direct access to target hardware during development | Slower debugging | Diagnostics tool, "Export diagnostics", fake-device test harness, CI Windows builds |
| Plugin crash during a live show | Loss of audio | Sandbox option, scanner process, recording on a separate thread |
| Graph edits during a live show | Clicks/dropouts | Atomic swap + wire crossfade + Show Lock |
| Node-editor usability with 32+ channels | Wire spaghetti | Channel bundles, groups/hierarchy, templates, minimap |
| Licence compatibility | Can't publish | Verify all SDK terms in Phase 0 |

---

## 11. Decision Log

| # | Decision | Status |
|---|---|---|
| D1 | C++20 + JUCE 9.0.2 + CMake | **Accepted** |
| D2 | Single primary device; ASIO default, WASAPI supported | **Accepted** |
| D3 | FireWire via owner-supplied vendor ASIO drivers | **Accepted** |
| D4 | Stream-to-disk via RAM ring buffers + optional pre-roll | Proposed |
| D5 | BWF + auto RF64, mono files, 24-bit raw / 32-bit float processed | Proposed |
| D6 | Hierarchical node graph is the primary UI; faces tear off into panels | **Accepted** |
| D7 | Cycles blocked; PDC in compiler; flattening of groups | Proposed |
| D8 | Plugins: VST3 + cross-platform LADSPA; sandbox option | **Accepted** (no VST2) |
| D9 | App Audio via Windows process/system loopback + ASRC | **Deferred** (backlog, §14.1) |
| D10 | Record only, no playback | **Accepted** |
| D11 | Open source, AGPLv3 | **Accepted** |
| D13 | No console view; node graph + user panels only | **Accepted** |
| D14 | Hardware test results arrive as GitHub issues with `spm-diag` reports | **Accepted** |
| D12 | Inter font, Lucide icons, dark theme default | Proposed |

---

## 12. Change History
- **v0.6**: App Audio capture (the old Phase 7) removed from the plan and moved to the backlog (§14.1). Later phases renumbered: Polish & release is now Phase 7, Cross-platform is Phase 8.
- **v0.5**: Phase 0b (FireWire) deferred until the hardware arrives; it still runs alongside later phases and is required before release. Phase 1's 1-hour 32-channel hardware test moves to Phase 0b; Phase 1 instead uses a simulated 32-channel device plus a 10-minute Scarlett run.
- **v0.4**: Confirmed the multiple-capture-points interpretation (R12). From the first Scarlett test run: Windows shared-mode audio (fixed 10 ms buffer, dropouts) is confirmed unsuitable for live paths, and spm-diag now ranks and recommends ASIO. Scarlett Solo on Focusrite USB ASIO ran cleanly at 192, 128 and 64 samples (reported round trip 920 / 696 / 376 samples). USB 1 ms frame jitter is now tolerated by the late-callback check.
- **v0.3**: Named *Stage Plot Mixer*; public repo; AGPLv3 accepted; JUCE 9.0.2 (bundles ASIO headers); VST3 + LADSPA only; multiple capture points; no console view; test hardware is the Scarlett Solo for now, with FireWire validation later (Phase 0b).
- **v0.2**: Incorporated requirements: live sound + home studio; target hardware list; 32 ch @ 48 kHz; record only; app audio input; open source; VST/LADSPA; node-based hierarchical UI; font choice.
- **v0.1**: Initial draft.

---

## 13. Open Questions

None at present. (Resolved in v0.4: capture points (R12) confirmed as Recorder nodes anywhere in the graph. App Audio nodes were part of that answer and moved to the backlog in v0.6.)

---

## 14. Backlog

Ideas that are designed or discussed but not scheduled. Each one can become a phase later if it's needed.

### 14.1 App Audio capture (was Phase 7)

*Removed from the plan in v0.6: not a requirement for now.*

The requirement: files played in other programs (media player, browser, backing-track software) must be usable as **inputs** to the mixer.

**App Audio source node:** two modes. **Any number of App Audio nodes** can run at once (e.g. one per application plus the whole-system mix), and each can feed any part of the graph. Together with Recorder nodes placed anywhere (§5.4), signals can be captured from as many points as needed (R12).

| Mode | How it works | Requirement |
|---|---|---|
| **Specific application** ✅ | Captures only the chosen program's audio (e.g. VLC) using Windows *process loopback* capture | Windows 10 2004+ / Windows 11 |
| **Whole system output** | Captures everything playing on a chosen Windows output device (WASAPI loopback) | Any Windows 10/11 |

- The captured audio runs on Windows' clock, not the ASIO device's clock, so it passes through an **adaptive resampler (ASRC)** that follows the drift. This adds ~10–20 ms of latency **to that source only**, which is fine for pre-recorded material.
- **Avoiding double playback:** the other application should play to a Windows device that isn't audible, or be muted locally. The node shows a hint about this. A bundled virtual audio device is a possible later addition.
- Cross-platform later: macOS Core Audio taps (macOS 14.2+), Linux PipeWire monitor streams.

- **Threading:** one capture thread per App Audio node (WASAPI loopback reads → ASRC FIFO). The audio thread only reads the FIFO.
- **Where the code would go:** a `/capture` module (WASAPI loopback, process loopback), Windows-only at first.
- **Risk:** clock drift causing clicks over time. Mitigation: adaptive ASRC with drift tracking, plus a soak test.
- **Exit test if scheduled:** a 1-hour capture from a media player with no drift or clicks, alongside the ASIO device.
- **Related:** a bundled virtual audio device (to avoid double playback) would belong with this.
