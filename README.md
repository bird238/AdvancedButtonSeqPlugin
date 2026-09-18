# Advanced Button Seq Plugin 

**Advanced Button Seq** is a standalone, single-module VCV Rack 2 plugin containing a 6-channel, 128-step graphical step sequencer. The plugin engine is built on top of a trimmed subset of internal utility code from the open-source Impromptu Modular plugin (by Marc Boulé) and extended with an original graphical step editor, dual pattern banks, extended per-step parameters, an audio-rate CV cyclic sampler with live oscilloscope, and native/MIDI file export.

> **Disclaimer**: This project is an independent open-source module created by bird238. It is **not** affiliated with, endorsed by, or officially connected to Marc Boulé, Impromptu Modular, VCV, or Andrew Belt.

---

## Features

- **6 Channels × 128 Steps**: Live performance graphical step sequencer organized into 8 selectable pages of 16 steps per channel.
- **Dual Pattern Banks (A/B)**: Each channel features two independent 128-step pattern banks (A and B), complete with separate gate and CV data, switchable live via panel button or CV trigger.
- **Rich Per-Step Data**:
  - Gate state (on/off)
  - CV1 pitch (1V/oct with on-screen volts and note-name display, e.g., `C#4`)
  - CV2 and CV3 parameters
  - Gate Length / Tie knob (max setting enables Tie/Glide, holding gate high and gliding CV1 into the next step if gated)
  - Recorded "CV cont" continuous value
- **Live Recording & Performance Inputs**:
  - Big red **BIG BUTTON** for live manual step writing (captures gate state and the 3 global CV inputs into the active step).
  - External **GATE IN** jack with selectable mode: *Normal* (alternate trigger for BIG BUTTON) or *Record Gate Length* (measures incoming gate duration and writes step gate length/legato ties in real-time).
  - **SNAP**: Quantizes live BIG BUTTON and DEL writes to the nearest clock edge.
  - **FILL**: Retriggers the active channel gate on every clock pulse while held.
  - **MEM**: Toggles whether live FILL hits are written into step memory or played live only.
  - **RND**: Applies a knob- and CV-controlled probability (0–100%) of toggling the active step's gate on each clock.
  - **CLEAR & Shift Controls**: Per-step swap arrows, active channel clear button, and global `<<<SHIFT` / `SHIFT>>>` buttons to rotate all 128 steps of a channel with wraparound.
- **Sample & Hold Mode**: Optional per-CV-row (CV1, CV2, CV3) S&H mode across the output matrix that holds CV output values until the next gate edge.
- **"CV cont" Cyclic Sampler & Oscilloscope**:
  - Audio-rate continuous loop recorder per channel with a 3-position mode switch (*Off/Playback*, *Record*, *Pass-through*).
  - Armed recording automatically starts at the next loop start (step 0), records exactly one sequence loop, and auto-disables at End-Of-Cycle.
  - Integrated live oscilloscope widget displays the active CV-cont output waveform with adjustable rolling time window (~0.5s to ~15s) or sequence-synced mode.
- **6 × 5 Output Matrix**: Simultaneous live output jacks for all 6 channels across 5 signal rows (CV1, CV2, CV3, CV-cont, Gate), plus a shared End-Of-Cycle (EOC) trigger output.
- **Rack Undo/Redo Integration**: All panel edits (gate toggles, knob adjustments, step shifts, pattern clear, clipboard paste) push snapshots to VCV Rack's native undo system (`Ctrl+Z` / `Cmd+Z`).
- **Copy / Paste Operations**:
  - Panel buttons copy and paste the currently visible 16-step page via system clipboard.
  - Right-click context menu options ("Copy channel" / "Paste channel") handle full 128-step channel and bank transfers.
- **File Export & Import**:
  - **SAVE MIDI**: Exports the active channel (up to current LEN) to Standard MIDI File format 0 (single track, 96 PPQ, 16th-note steps), merging tied and glided steps into single sustained notes.
  - **`.abseq` JSON**: Saves and loads complete 128-step channel patterns.
  - **`.abscv` JSON & WAV**: Exports and imports CV-cont cyclic sampler loops in native JSON or mono 16-bit PCM WAV format (using Eurorack ±10V CV to WAV `[-1.0, 1.0]` standard). WAV loader automatically accepts mono/stereo 8/16/24/32-bit PCM and 32-bit float audio files.
- **Context Menu Options**: Retrigger gates on reset toggle, follow active step mode (auto-switches grid pages to follow sequence playback), clipboard operations, and file I/O dialogs.

---

## Build & Installation

No compiled binaries are distributed in this repository. The plugin must be built from source using the official VCV Rack SDK.

### Prerequisites

- Git
- C++ build tools (`make`, `gcc` / `clang`)
- [VCV Rack SDK](https://vcvrack.com/downloads/) (version 2.4.0 or later, matching your installed Rack version)

### Building from Source

1. Clone the repository:
   ```bash
   git clone https://github.com/bird238/AdvancedButtonSeqPlugin.git
   cd AdvancedButtonSeqPlugin
   ```

2. Build the plugin against your Rack SDK:
   ```bash
   make RACK_DIR=<path-to-Rack-SDK>
   ```

3. Install the plugin by copying or symlinking the output build directory into your local VCV Rack user plugins directory:
   - **Linux**: `~/.local/share/Rack2/plugins-lin-x64/`
   - **macOS**: `~/Documents/Rack2/plugins-mac-*/`
   - **Windows**: `Documents\Rack2\plugins-win-x64\`

4. Restart VCV Rack 2.

---

## Credits & Provenance

- **Core Engine Utilities**: Built on top of a trimmed subset of open-source engine utilities from [Impromptu Modular](https://github.com/MarcBoule/ImpromptuModular) by **Marc Boulé**.
- **Sequencer Architecture & UI**: Original design and implementation by **bird238**.
- **Attribution & Licenses**: See [`LICENSE.md`](LICENSE.md) for full per-file copyright, license, and provenance breakdowns.

> **Note**: This plugin is an independent work and is not affiliated with, endorsed by, or connected to Marc Boulé, Impromptu Modular, VCV, or Andrew Belt.


