# ForgeAudio Naming

- Public API functions use `forge_`.
- Non-static internal functions shared across translation units use `fa_`.
- File-local functions and variables use `lower_snake_case`.
- Macros and compile-time constants use `UPPER_SNAKE_CASE`.
- Public and internal types use Pascal/Camel case, such as `ForgeVoice` and `DspDelay`.
- Keep established compatibility wrapper macro families, such as `forge_memcpy`, in their existing style.
