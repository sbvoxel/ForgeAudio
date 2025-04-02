# SbAudio

SbAudio is a fork of [FAudio](https://github.com/FNA-XNA/FAudio) to explore an audio solution independent of the XAudio framework for games and other applications.

The development of this experimental library is at the moment slow and periodic.

Platforms

- Windows 10+
- Linux


## Alternatives
- Commercial
	- WWise
	- FMOD

- LabSound
    - Fork of WebKit's WebAudio implementation
    - Graphs galore

- SoLoud
    - Easy to use. Not the best design for AAA games I hear (?)
    - Very little development, but considered stable by author

- OpenAL
    - Very old
    - Seems to be receiving lots of development recently at least

- FAudio
    - What this project is forked from

* miniaudio
    - Tries to do everything itself
    - Odd looking project

- Low-level (wrap the drivers / platform APIs):
    - libsoundio
    - PortAudio
    - RtAudio
    - SDL

- Platform APIs:
    - Linux
       * PulseAudio
       * PipeWire
    - Windows
    	* WASAPI

* Middleware
    - Steam Audio
        - Steam Audio is a spatial audio middleware that adds realistic 3D sound propagation,
        occlusion, and HRTF-based rendering to existing audio engines.
    - libsamplerate
        - An audio Sample Rate Conversion library
