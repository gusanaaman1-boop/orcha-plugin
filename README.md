# ORCHA

Drop 1–3 samples, choose the vibe, click once — get 12 seeded, host-synced
rhythm-loop options you can preview and drag straight into Cubase as WAV.

VST3 / AU / Standalone, JUCE 9, C++20, CMake. Design: `docs/DESIGN.md`.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

Targets: `Orcha_VST3`, `Orcha_AU`, `Orcha_Standalone`,
`OrchaTests` (engine checks), `OrchaShot` (deterministic UI renderer + host
smoke test incl. state round-trip).

```bash
./build/OrchaTests_artefacts/Release/OrchaTests
./build/OrchaShot_artefacts/Release/OrchaShot ui-shots kick.wav snare.wav hat.wav
```

## Architecture in one breath

Message thread owns the model; a 2-thread pool decodes/analyzes samples and
generates+renders+caches all options; the audio thread only mixes the active
preview loop through a SpinLock-tryEnter buffer handoff. Every option is
deterministic from its seed; state stores seeds, favorites, settings and
sample paths, and audio is rebuilt on load. Rendered WAVs live in the temp
cache and are dragged out with
`DragAndDropContainer::performExternalDragDropOfFiles` (canMove = false).
