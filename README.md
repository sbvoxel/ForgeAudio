# ForgeAudio

> Status note: ForgeAudio is in active early development. Large pieces of the
> inherited FAudio/XAudio-shaped surface are being removed, renamed, or
> redesigned into ForgeAudio-owned APIs. Treat the public API as pre-1.0 and in
> flux. Furthermore, this README is currently AI-authored and may need later
> revision for precision.

ForgeAudio is a general-purpose audio runtime for games and interactive
applications.

It began as a fork of [FAudio](https://github.com/FNA-XNA/FAudio), a mature
reimplementation of Microsoft's game-audio runtime APIs. ForgeAudio keeps the
useful low-level foundation from FAudio while moving toward a smaller,
engine-facing design of its own.

The goal is a compact, explicit audio library that can sit underneath a game
engine or custom tool: voices, submixes, channel matrices, effects, spatial
audio, device output, deterministic tests, and enough control to build a
higher-level audio system on top.

## Status

ForgeAudio is an independent audio library derived from FAudio.

Current state:

- The public headers, build targets, symbols, and result types use ForgeAudio
  names and conventions.
- XACT, XNA song support, MSADPCM source support, WMA/XMA public entry points,
  legacy compatibility utilities, generated project files, and inherited
  packaging have been removed.
- The build has been simplified around CMake and C23.
- SDL3 is the default portable backend path, with a native Win32 path available
  behind `PLATFORM_WIN32`.
- Effects, spatial audio, audio batches, format handling, parameter automation,
  and testing APIs are being shaped for ForgeAudio rather than preserved as
  direct FAudio/XAudio surface area.

Upstream FAudio remains useful reference material and a source of fixes, but
compatibility with it or XAudio is not a design goal.

## Features

- Source, submix, and master voices.
- Voice sends, output matrices, and per-send filters.
- Effect chains with ForgeAudio-owned built-in effects.
- Spatial audio helpers.
- Deferred audio batches for synchronized control changes.
- Gain automation for voice volume, channel volume, and output matrices.
- Source-rate target and ramp automation for pitch/resampling changes.
- Target/de-zip APIs for common smoothed gain changes.
- Explicit frame-duration ramps and millisecond convenience ramps.
- Source fade-stop automation for "fade, then stop on the audio timeline."
- Device-backed rendering through platform backends.
- Device-free deterministic render tests under `FORGE_AUDIO_TESTING`.

## Runtime Model

ForgeAudio uses a practical voice graph. Source voices consume submitted audio
buffers, submix voices combine and process routed audio, and a master voice
feeds the platform device. Voices route to other voices through sends. Output
matrices control how rendered source channels map into destination channels.

Control changes can be immediate or deferred through audio batches. Deferred
batches make related commands visible at the same processing-pass boundary,
which is useful for synchronized starts, stops, routing changes, and automation.

Gain-like parameters have two families of motion APIs:

- `_frames` APIs specify an exact number of rendered output sample frames.
- `_ms` APIs convert milliseconds to output frames at the public API boundary.

Existing setters such as `forge_voice_set_volume` remain hard setters. Target
APIs such as `forge_voice_set_volume_target` use ForgeAudio's internal default
de-zip duration.

Source-rate automation follows the same batch and timing model, but it is
ratio-domain and updates the source resampler step as it advances. Use
`forge_source_voice_set_rate` for a hard pitch/rate change, or
`forge_source_voice_set_rate_target`, `forge_source_voice_ramp_rate_frames`, and
`forge_source_voice_ramp_rate_ms` for smoothed motion.

## Building

ForgeAudio uses CMake 3.20+ and C23.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Useful options:

| Option | Default | Description |
| --- | --- | --- |
| `BUILD_SHARED_LIBS` | `ON` | Build ForgeAudio as a shared library. |
| `BUILD_TESTS` | `OFF` | Build CTest test executables. |
| `ENABLE_ASAN` | `OFF` | Build with AddressSanitizer when supported by the compiler. |
| `ENABLE_PULSEAUDIO` | `ON` | Enable PulseAudio support in the SDL3 backend path. |
| `ENABLE_WASAPI` | `ON` | Enable WASAPI support in the SDL3 backend path. |
| `PLATFORM_WIN32` | `OFF` | On Windows, use the native Win32 platform path instead of SDL3. |
| `LOG_ASSERTIONS` | `OFF` | Log assertions instead of binding to the platform assert. |
| `FORCE_ENABLE_DEBUGCONFIGURATION` | `OFF` | Enable debug configuration APIs in all build types. |
| `DUMP_VOICES` | `OFF` | Dump source voices to RIFF WAVE files for debugging. |

## Testing

Build tests with `BUILD_TESTS=ON`:

```sh
cmake -B build -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The test suite includes white-box lower-level probes and an engine-level render
harness. The render harness creates a virtual master voice and advances the real
engine processing path synchronously, with no audio device and no wall-clock
dependency. This is used to test batch timing, source rendering, automation,
fade-stop behavior, format validation, and lifecycle failure paths
deterministically.

## Minimal Shape

The public API is still moving, but the intended shape is explicit: create an
engine, create a master voice, create source/submix voices, submit buffers, then
start the engine and voices. Error handling and buffer setup are omitted here.

```c
ForgeAudioEngine *audio = NULL;
ForgeMasterVoice *master = NULL;
ForgeSourceVoice *source = NULL;
ForgeBuffer buffer = {0};

ForgeAudioFormat format = {
    .format_tag = FORGE_AUDIO_FORMAT_IEEE_FLOAT,
    .channels = 2,
    .sample_rate = 48000,
    .bits_per_sample = 32,
    .block_align = 2 * sizeof(float),
    .average_bytes_per_second = 48000 * 2 * sizeof(float),
};

forge_audio_create(&audio, 0);
forge_audio_create_master_voice(audio, &master, 2, 48000, 0, 0, NULL);
forge_audio_create_source_voice(audio, &source, &format, 0,
                                FORGE_AUDIO_DEFAULT_FREQ_RATIO,
                                NULL, NULL, NULL);

/* Fill buffer.audio_data, buffer.audio_bytes, and any play/loop fields. */
forge_source_voice_submit_buffer(source, &buffer);
forge_source_voice_start(source, 0, FORGE_AUDIO_BATCH_IMMEDIATE);
forge_audio_start_engine(audio);
```

## Automation Example

Use `_ms` APIs for ordinary application code and `_frames` APIs when exact frame
counts matter.

```c
/* Fade a voice out over 50 ms. */
forge_voice_ramp_volume_ms(voice, 0.0f, 50.0, FORGE_AUDIO_BATCH_IMMEDIATE);

/* Exact deterministic ramp over 240 rendered output frames. */
forge_voice_ramp_volume_frames(voice, 1.0f, 240, FORGE_AUDIO_BATCH_IMMEDIATE);

/* Smooth to a target using ForgeAudio's internal de-zip duration. */
forge_voice_set_volume_target(voice, 0.5f, FORGE_AUDIO_BATCH_IMMEDIATE);

/* Ramp source playback rate over 100 ms. */
forge_source_voice_ramp_rate_ms(source, 1.5f, 100.0, FORGE_AUDIO_BATCH_IMMEDIATE);

/* Fade out, then stop the source voice on the audio timeline. */
forge_source_voice_fade_stop_ms(source, 0.0f, 50.0, FORGE_AUDIO_BATCH_IMMEDIATE);
```

`forge_audio_ms_to_frames` exposes the same millisecond-to-frame conversion rule
used by the `_ms` wrappers.

## Platforms

Currently targeted:

- Windows 10+
- Linux

Other platforms may work over time, but they are not the focus yet.

## Goals

- Stay small enough to understand and embed.
- Keep a practical voice, submix, routing, and effect model for engine audio.
- Make spatial audio, routing, effect processing, automation, and format
  handling explicit.
- Prefer predictable runtime behavior over a large built-in authoring model.
- Add higher-level features when they are justified by real engine use.
- Remain permissively licensed.

## Non-Goals

- ForgeAudio is not an audio authoring suite like FMOD or Wwise.
- It is not a WebAudio-style graph engine by default.
- It is not a dedicated acoustic propagation system like Steam Audio.
- It is not a compatibility layer for another audio API.

## Format Scope

ForgeAudio currently focuses on uncompressed PCM and IEEE float source formats.
Several inherited compressed or compatibility-oriented surfaces have been
removed or explicitly rejected as unsupported, including WMA/XMA public entry
points and MSADPCM source support.

## Comparison

### FAudio

FAudio accurately provides DirectX audio runtime behavior for FNA and related
projects. ForgeAudio benefits from that foundation, but it is free to remove
compatibility surface area, reshape the public API, simplify build and platform
assumptions, and add engine-oriented features that would not belong in upstream
FAudio.

### SoLoud

SoLoud is a compact, easy-to-use game audio engine with a friendly high-level
API. ForgeAudio is lower-level and more explicit; it is intended as a foundation
for custom engine audio layers rather than a complete game-audio facade by
itself.

### LabSound

LabSound is a graph-based audio engine derived from WebAudio. Its graph model is
useful for modular synthesis, procedural audio, and flexible signal routing.
ForgeAudio keeps the default runtime model closer to voices, submixes, matrices,
and effects. Graph-like behavior can be added above that layer where it is
useful.

### OpenAL Soft

OpenAL Soft provides a mature OpenAL implementation with strong positional audio
support. ForgeAudio follows a voice/submix/matrix model instead, which can be a
better fit for engines that want direct control over routing and mixing.

### miniaudio, SDL, PortAudio, RtAudio, libsoundio

These projects are primarily lower-level audio I/O or utility libraries.
ForgeAudio sits above that layer: it provides voices, mixing, spatial
calculations, effects, automation, and format handling rather than only device
access.

### FMOD And Wwise

FMOD and Wwise are commercial middleware and authoring ecosystems. ForgeAudio is
a runtime library for developers who want to own the engine-facing audio system.

### Steam Audio

Steam Audio focuses on spatial acoustics such as HRTF rendering, occlusion,
reflection, and propagation. ForgeAudio may be used alongside systems like that,
but it is primarily an audio runtime rather than a dedicated acoustic simulation
package.

## Custom Effects

ForgeAudio currently treats effects as opaque handles created by ForgeAudio
itself. Built-in effects can be attached to voices and submixes, configured
through the voice effect APIs, and destroyed or transferred into an effect
chain.

Earlier versions inherited a public custom-effect vtable from the compatibility
API surface. That exposed an advanced extension point where application code
could build its own effect object by filling out a struct of callbacks.
ForgeAudio removed that public vtable for now. The old shape was powerful, but
it also exposed internal engine protocol before ForgeAudio had a clear
independent design for custom DSP.

This does not mean custom effects are off the table. It means ForgeAudio is not
committing to that inherited ABI as the long-term answer. If custom third-party
effects become a real goal, they should return as a ForgeAudio-designed API:
probably a versioned descriptor or registration function that produces an opaque
`ForgeEffect *`, with clear rules for formats, buffers, real-time processing,
parameters, ownership, and thread safety.

## License

ForgeAudio inherits FAudio's permissive zlib-style license. See [LICENSE](LICENSE)
for details.
