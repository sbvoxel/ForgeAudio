SbAudio is a fork of FAudio, and FAudio is an XAudio2 re-implementation. Unlike FAudio, SbAudio intends to be its own solution, independent of XAudio2.

It's worthwhile to note down how XAudio2 compatibility requirements have constrained or had an effect on FAudio, because it may tell us in what ways SbAudio may want to do things differently, with it being free from XAudio2 compatibility.

## Resampler/Mixer

https://github.com/FNA-XNA/FAudio/issues/308

**flibitijibibo** says:

Those end up being pretty heavy-duty in terms of dependencies - FAudio is in the same boat as SDL where we really need everything to be either self-contained or something that's zlib-licensed (or stb-style public domain), since many targets can't bring something like ffmpeg into the mix. But, being in the same boat, I've kept a close eye on how SDL has dealt with resampling in particular, and the result is that we have 2 options:

1. Add a path for libsamplerate, can be dynamically loaded: https://github.com/libsdl-org/SDL/blob/main/src/audio/SDL_audiocvt.c#L497
2. Make resampling an FAudio_platform_* function, and add pitch shift support to SDL_AudioStream: https://github.com/libsdl-org/SDL/blob/main/include/SDL3/SDL_audio.h#L685

Way back in the day I attempted to use option 2 and the result was pretty limited; we have to be able to adjust the sample rate pretty much all the time, so most resampling libraries don't cut it in terms of all the fussy things XAudio needs. I did my best to take notes from Chris Robinson and OpenAL Soft (without actually using the code, for licensing reasons), but yeah, it's definitely got some sharp edges.

Mixing is a bit tougher because of the way XAudio2 processes channels; mixing isn't hard but writing handwritten SIMD can be sometimes... unlike resampling I actually don't think there's anything that does exactly what XAudio does, but at the same time it's much easier to get correct than resampling.
