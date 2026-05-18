# ForgeAudio Naming

- Public API functions use `forge_`.
- Non-static internal functions shared across translation units use `fa_`.
- File-local functions and variables use `lower_snake_case`.
- Macros and compile-time constants use `UPPER_SNAKE_CASE`.
- Public and internal types use Pascal/Camel case, such as `ForgeVoice` and `DspDelay`.
- Keep established compatibility wrapper macro families, such as `forge_memcpy`, in their existing style.

# ForgeAudio Runtime Semantics

- Engine/batch code owns timeline semantics: command ordering, batch boundaries, ms-to-frame conversion, ramp duration, and timeline advancement. Effects own parameter meaning and DSP-safe application: validation, interpolation domain, coefficient/state updates, and whether a field is smoothable, hard-set, rebuild-only, or crossfade-only.
- Hard setters cancel matching automation only when the setter applies on the audio timeline. Do not delete matching pending deferred batch commands; deferred batches remain isolated until `forge_audio_apply_batch` moves them to the ready queue.
