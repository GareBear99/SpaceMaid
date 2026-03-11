SpaceMaid (JUCE) — Automatic Space Blending (SPACE-inspired behavior, original design)

Goal:
- “Blend and bring out the sound automatically” with minimal controls.
- Reverb tail is auto-EQ’d + auto-ducked for clarity and translation.

Controls:
- Blend: main amount (also drives intelligent decay/ducking/EQ)
- Size: room->hall macro
- Clarity: clean->lush (tail EQ + duck strength)
- Motion: adds wet modulation for width/interest
- Keep Punch: stronger transient protection (more predelay + more ducking)
- Mix: wet/dry
- Output: output gain

Architecture (RT-safe):
- No dynamic allocations in processBlock.
- Coefficient updates are gated and happen outside sample loop.
- Per-block analysis (RMS, crest, transient proxy) drives macro targets.

Build (CMake):
1) Install JUCE locally (JUCE 7 recommended).
2) Configure:
   Option A:
     cmake -B build -S . -DJUCE_DIR=/path/to/JUCE
   Option B:
     clone JUCE next to this project and run:
     cmake -B build -S . -DJUCE_PATH=../JUCE
3) Build:
   cmake --build build --config Release

Formats: AU + VST3 + Standalone
