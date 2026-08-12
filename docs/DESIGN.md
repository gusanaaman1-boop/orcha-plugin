# ORCHA — Design Response (pre-implementation)

This is the design answer required by the master build prompt, section
"Implementation response format". Implementation follows in vertical slices.

## 1. Architecture diagram

```
                         ┌────────────────────────────────────────────┐
                         │                PluginEditor                 │
                         │  SampleCard ×3   GenerationStrip   OptionGrid (12 × OptionCard)
                         │      │                │                │        │
                         └──────┼────────────────┼────────────────┼────────┼───┘
                                │ load/drop      │ generate       │ play   │ drag
                                ▼                ▼                ▼        ▼
┌──────────────┐   async   ┌──────────────────────────────┐   ┌──────────────────┐
│ SampleLoader │──────────▶│        PluginProcessor        │   │ ExternalDragSource│
│ (ThreadPool) │           │  owns: samples, options,      │   │ performExternal-  │
└──────┬───────┘           │  ThreadPool, PreviewPlayer,   │   │ DragDropOfFiles   │
       │                   │  RenderCache, PluginState     │   │ (canMove=false)   │
       ▼                   └──────┬───────────────┬────────┘   └──────────────────┘
┌──────────────┐                  │ GenerateJob   │ audio thread
│SampleAnalyzer│                  ▼ (ThreadPool)  ▼
│ role assign  │           ┌──────────────┐  ┌──────────────┐
└──────────────┘           │LoopGenerator │  │PreviewPlayer │  RT-safe handoff:
                           │ + RhythmStyle│  │ loops one     │  SpinLock tryEnter,
                           │ + SectionProfile│ rendered buf │  old buffers drained
                           │ + PatternValidator│ synced to  │  on message thread
                           └──────┬───────┘  │ host PPQ     │
                                  ▼          └──────────────┘
                           ┌──────────────┐
                           │ LoopRenderer │──▶ RenderCache (WAV files, seed-named)
                           └──────────────┘
```

Everything above the audio-thread line runs on the message thread or the
ThreadPool. `processBlock()` only mixes the current preview buffer.

## 2. Repository structure

```
Orcha/
  CMakeLists.txt              VST3 + AU + Standalone + OrchaTests console app
  docs/DESIGN.md              this file
  Source/
    Core/OrchaVersion.h.in    build identity (same scheme as EDGE)
    Model/InputSample.h       immutable loaded sample + analysis + role
    Engine/
      SampleAnalyzer.{h,cpp}  transients, centroid, LF energy, one-shot test, role assignment
      SampleLoader.{h,cpp}    WAV/AIFF/FLAC decode on worker thread
      Pattern.h               Event / Pattern value types (seed, mode, family…)
      RhythmStyle.{h,cpp}     family skeletons: EDM, ARABIC (Maqsum/Baladi/Sa'idi/
                              Malfuf/Ayyub/Ciftetelli-inspired), MEDITERRANEAN, AFRO, HYBRID
      SectionProfile.h        DROP / BREAK / BUILD / GROOVE behaviour constants
      LoopGenerator.{h,cpp}   deterministic seeded generation pipeline (10 steps)
      PatternValidator.{h,cpp} collisions, density bounds, boundary, silence floor
      LoopRenderer.{h,cpp}    offline render at host SR/tempo, fades, headroom
      RenderCache.{h,cpp}     seed-named WAVs in the user cache dir
    Playback/PreviewPlayer.{h,cpp}  RT-safe loop preview, host-synced
    Ui/  Theme, SampleCard, GenerationStrip, OptionCard
    PluginProcessor.{h,cpp}  PluginEditor.{h,cpp}
    Tools/test_engine.cpp    determinism / diversity / validator / render tests
```

## 3. Threading and rendering model

- **Message thread**: all UI, state save/load, drag initiation, cache bookkeeping.
- **ThreadPool (2 threads)**: sample decode + analysis; generation + offline
  render + WAV write for all 12 options (one job per generate click; cancellable
  flag checked between options).
- **Audio thread**: `processBlock()` mixes the active preview loop only. The
  loop buffer arrives as a `ReferenceCountedObjectPtr` through a
  SpinLock-`tryEnter` slot (skip, never block); retired buffers are parked in an
  outgoing slot and released by a message-thread timer, so no allocation or
  deallocation ever happens on the audio thread. Tempo/PPQ are read from the
  playhead each block and published to the UI via atomics.
- Renders happen at the tempo/SR captured when GENERATE is clicked. Tempo
  changes >0.5 BPM mark options stale; re-render lazily on preview/drag.

## 4. Sample input drag flow

`SampleCard` implements `juce::FileDragAndDropTarget` (the editor also accepts
drops anywhere and routes to the first free card). Accepted: .wav/.aiff/.aif/
.flac. Explorer/Finder drags give paths directly; Cubase MediaBay drags are
accepted whenever Cubase exposes a real file path (it does for its media
folders) — when only an internal VST3 payload is offered, we cannot read it
(limitation, §8) and the Load button (async `FileChooser`) is the fallback.
Drop → `SampleLoader` job → decode + `SampleAnalyzer` → immutable
`InputSample` swapped into the slot on the message thread → roles reassigned →
card repaints waveform.

## 5. Generated-loop drag-out flow

Card `mouseDrag` (past 8 px) → ensure the option's WAV exists in `RenderCache`
(it is written at generate time; re-rendered on demand if evicted) →
`juce::DragAndDropContainer::performExternalDragDropOfFiles({wav}, /*canMove*/ false)`
from the editor. Cubase imports the file as audio at the drop position. The
WAV is 24-bit at the host sample rate, exactly bars × 4 beats long at the
render tempo, so it lines up with the grid.

## 6. Serialized state schema (plug-in state, ValueTree → XML)

```xml
<ORCHA version="0.1.0" schema="1">
  <settings mode="DROP" family="EDM" energy="0.6" density="0.5"
            randomness="0.3" bars="1"/>
  <samples>
    <sample slot="0" path="/abs/path/Kick.wav" role="AUTO"/>  <!-- ×0..3 -->
  </samples>
  <options tempo="126.0" sampleRate="48000">
    <option seed="0x9E3779B97F4A7C15" name="DROP 01" favorite="1"/> <!-- ×N -->
  </options>
</ORCHA>
```

Only seeds and metadata persist; audio is rebuilt deterministically from
(seed, settings, samples, tempo). Missing source files → card shows a
missing-file state, options stay listed but disabled until samples return.

## 7. First vertical slice + acceptance criteria

Slice 1 (this session): buildable VST3/AU/Standalone with the full main
screen; drop/load 1–3 samples; DROP + BREAK + BUILD + GROOVE; all five family
chips wired (EDM, ARABIC, HYBRID fully tuned first); ENERGY/DENSITY/RANDOMNESS
+ 1/2/4 bars; GENERATE produces 12 seeded, validated, meaningfully different
options rendered to cached WAVs; per-card play (host-synced), favorite,
regenerate-one, external drag-out; GENERATE MORE keeps favorites; state
save/restore of everything in §6; `OrchaTests` console suite green
(determinism, diversity, boundary, headroom, no-audio-thread-work by design).

Accepted when: `cmake --build` succeeds for all targets; `OrchaTests` passes;
`auval`/pluginval-style smoke (host test) passes locally; UI matches the
approved mockup zones.

## 8. Cubase/MediaBay limitations that cannot be guaranteed

- MediaBay drags that carry only Steinberg-internal payloads (no filesystem
  path) cannot be read by a JUCE `FileDragAndDropTarget`; the Load button is
  the guaranteed path. No private MediaBay APIs are used.
- Drop position snapping inside Cubase is host behaviour (snap settings), not
  controllable from the plug-in.
- While the host transport is stopped Cubase does not advance PPQ, so preview
  free-runs at the last known tempo and re-locks to PPQ on play.
