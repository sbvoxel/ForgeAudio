# ForgeAudio

ForgeAudio is a general-purpose audio runtime for games and interactive applications.

It began as a fork of [FAudio](https://github.com/FNA-XNA/FAudio), a mature reimplementation of Microsoft's game-audio runtime APIs. ForgeAudio keeps the useful low-level foundation from FAudio while moving toward a smaller engine-facing design of its own.

The goal is a compact, explicit audio library that can sit underneath a game engine or custom tool: voices, submixes, channel matrices, effects, spatial audio, device output, and enough control to build a higher-level audio system on top.

## Status

ForgeAudio is still early as an independent fork, but it is not a greenfield rewrite.

The current codebase has already diverged from upstream FAudio in several ways:

- Public headers and build targets have been renamed for ForgeAudio.
- XACT and related tooling have been removed.
- The C# wrapper, Visual Studio solution files, Xcode project files, and some packaging inherited from upstream have been removed.
- The build has been simplified around CMake and C23.
- SDL3 is the default portable backend path, with a native Win32 path still available behind `PLATFORM_WIN32`.
- Several upstream FAudio fixes have been ported into the fork.

ForgeAudio is now allowed to diverge when that makes the library smaller, clearer, or better suited to engine use. Upstream FAudio remains useful reference material, but source compatibility and easy patch porting are no longer design constraints.

## Goals

- Stay small enough to understand and embed.
- Keep a practical voice, submix, routing, and effect model for engine audio.
- Make spatial audio, routing, effect processing, and format handling explicit.
- Prefer predictable runtime behavior over a large built-in authoring model.
- Add higher-level features when they are justified by real engine use.
- Remain permissively licensed.

## Non-Goals

- ForgeAudio is not an audio authoring suite like FMOD or Wwise.
- It is not a WebAudio-style graph engine by default.
- It is not a dedicated acoustic propagation system like Steam Audio.
- It is not a compatibility layer for another audio API.

## Platforms

Currently targeted:

- Windows 10+
- Linux

Other platforms may work over time, but they are not the focus yet.

## Building

ForgeAudio uses CMake.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Useful options:

- `BUILD_SHARED_LIBS`: build a shared library. Enabled by default.
- `BUILD_UTILS`: build the utility programs.
- `PLATFORM_WIN32`: use the native Win32 platform path instead of SDL3 on Windows.
- `LOG_ASSERTIONS`: log assertions instead of binding to the platform assert.
- `DUMP_VOICES`: dump source voices to RIFF WAVE files for debugging.

## Relationship To FAudio

FAudio is focused on accurately providing DirectX audio runtime behavior for FNA and related projects. That is valuable, and ForgeAudio benefits from that foundation.

ForgeAudio is different in intent. It is free to remove compatibility surface area, reshape the public API, simplify build and platform assumptions, and add engine-oriented features that would not belong in upstream FAudio.

Upstream FAudio fixes are still worth tracking. The fork is mature enough to build on, while upstream is stable enough that carrying useful fixes is manageable.

## Comparison

### SoLoud

SoLoud is a compact, easy-to-use game audio engine with a friendly high-level API. ForgeAudio is lower-level and more explicit; it is intended as a foundation for custom engine audio layers rather than a complete game-audio facade by itself.

### LabSound

LabSound is a graph-based audio engine derived from WebAudio. Its graph model is useful for modular synthesis, procedural audio, and flexible signal routing. ForgeAudio keeps the default runtime model closer to voices, submixes, matrices, and effects. Graph-like behavior can be added above that layer where it is useful.

### OpenAL Soft

OpenAL Soft provides a mature OpenAL implementation with strong positional audio support. ForgeAudio follows a voice/submix/matrix model instead, which can be a better fit for engines that want direct control over routing and mixing.

### miniaudio, SDL, PortAudio, RtAudio, libsoundio

These projects are primarily lower-level audio I/O or utility libraries. ForgeAudio sits above that layer: it provides voices, mixing, spatial calculations, effects, and format handling rather than only device access.

### FMOD And Wwise

FMOD and Wwise are commercial middleware and authoring ecosystems. ForgeAudio is a runtime library for developers who want to own the engine-facing audio system.

### Steam Audio

Steam Audio focuses on spatial acoustics such as HRTF rendering, occlusion, reflection, and propagation. ForgeAudio may be used alongside systems like that, but it is primarily an audio runtime rather than a dedicated acoustic simulation package.

## Custom Effects

ForgeAudio currently treats effects as opaque handles created by ForgeAudio itself. Built-in effects can be attached to voices and submixes, configured through the voice effect APIs, and destroyed or transferred into an effect chain.

Earlier versions inherited a public custom-effect vtable from the compatibility API surface. That exposed an advanced extension point where application code could build its own effect object by filling out a struct of callbacks. ForgeAudio removed that public vtable for now. The old shape was powerful, but it also exposed internal engine protocol before ForgeAudio had a clear independent design for custom DSP.

This does not mean custom effects are off the table. It means ForgeAudio is not committing to that inherited ABI as the long-term answer. If custom third-party effects become a real goal, they should return as a ForgeAudio-designed API: probably a versioned descriptor or registration function that produces an opaque `ForgeEffect *`, with clear rules for formats, buffers, real-time processing, parameters, ownership, and thread safety.

## License

ForgeAudio inherits FAudio's permissive zlib-style license. See [LICENSE](LICENSE) for details.
