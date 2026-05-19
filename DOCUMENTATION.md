# ForgeAudio Documentation

This file collects runtime model notes, usage examples, integration guidance,
and design context that would otherwise make the README too large.

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

Built-in effect automation follows the same batch and timing model for selected
cheap continuous parameters. Delay supports typed target/ramp automation for
`wet_dry_mix`, `feedback`, and `lowpass_hz`; `delay_ms` remains hard-set through
`ForgeDelayParameters` blob sets only.

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

## Shared Reverb Bus

For game mixes, prefer a shared reverb bus over one reverb per source. Create
normal dry/game buses as submix voices, create one shared reverb submix with a
built-in reverb effect, then route each source to both its dry bus and the
shared reverb submix. Keep the reverb effect itself mostly wet; control "how
much reverb" for each source or category by changing the source-to-reverb output
matrix/send level.

Use `forge_voice_set_output_matrix_target`,
`forge_voice_ramp_output_matrix_frames`, or
`forge_voice_ramp_output_matrix_ms` for reverb-send changes. Use deferred
batches when a mix state needs dry level, reverb send, send filters, and reverb
parameter targets/ramps to become visible together. This is the recommended
foundation for higher-level systems for caves, interiors, occlusion/obstruction
coloring, snapshots/mix states, and automatic environmental sweetening.

```c
/* Pseudo-code: creation details omitted. */
game_bus = create_submix_voice(...);
shared_reverb = create_submix_voice(..., mostly_wet_reverb_effect);
source = create_source_voice(..., sends(game_bus, shared_reverb));

ForgeAudioBatchId mix_batch = 42;
forge_voice_set_output_matrix_target((ForgeVoice *) source,
                                     (ForgeVoice *) game_bus,
                                     src_ch, bus_ch, dry_level, mix_batch);
forge_voice_ramp_output_matrix_ms((ForgeVoice *) source,
                                  (ForgeVoice *) shared_reverb,
                                  src_ch, bus_ch, reverb_send, 80.0,
                                  mix_batch);
forge_voice_ramp_reverb_parameters_ms((ForgeVoice *) shared_reverb,
                                      0, &room_reverb, 80.0, mix_batch);
forge_audio_apply_batch(audio, mix_batch);
```

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
