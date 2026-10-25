# VocalPiano (VST3 for FL Studio)

Sing or rap into a mixer track -> VocalPiano detects your pitch (YIN), plays it back as a
built-in piano, and records every note so you can export the "score" as a MIDI file.

## Build (easiest: no setup)
1. Create a free GitHub repo and push this folder.
2. Open the **Actions** tab -> "Build VocalPiano" -> download the `VocalPiano-VST3` artifact.

## Build locally (Windows, Visual Studio 2022 + CMake)
    cmake -B build
    cmake --build build --config Release
Result: `build/VocalPiano_artefacts/Release/VST3/VocalPiano.vst3`

## Install
Copy `VocalPiano.vst3` to `C:\Program Files\Common Files\VST3\`
FL Studio -> Options -> Manage plugins -> Find installed plugins.

## Use
1. Put VocalPiano on a mixer insert that carries your vocal (it's an effect plugin, because FL
   synths can't receive audio).
2. Sing. The piano plays what you sing (set **Dry Vocal** up if you want to hear both).
3. Play your project (so notes land on the beat grid), then click **Export MIDI** and drag the
   .mid file into the Piano Roll / Channel Rack. Swap in any piano you like.

Knobs: Threshold (gate level), Stability (how long a pitch must hold before it becomes a note),
Octave (shift), Piano (level), Dry Vocal (mix).
