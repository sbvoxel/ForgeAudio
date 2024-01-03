# SbAudio

SbAudio is a fork of FAudio for the purpose of exploring an audio solution independent of the XAudio framework. Acknowledging the strengths of XAudio's design and FAudio's implementation, I'm left wanting a standalone FAudio solution for those with no interest in XAudio compatibility.

The development of this experimental library will for now be slow and periodic.

Fork of: https://github.com/FNA-XNA/FAudio

============
Requirements
============

Windows
    Windows 10+

Linux

============
Alternatives
============

Commercial:
    WWise
    FMOD

LabSound
    * Fork of WebKit's WebAudio implementation
    * Graphs galore

SoLoud
    * Easy to use. Not the best design for AAA games I hear (?)

OpenAL
    * Now you're old
    * Seems to be receiving lots of development recently at least

FAudio
    * What this project is forked from

Low-level (wrap the drivers / platform APIs):
    libsoundio
    PortAudio
    RtAudio
    miniaudio
    SDL

Platform APIs:
    Linux
        PulseAudio
        PipeWire
    Windows
        WASAPI

Platform APIs can be understood very simply:

They are userland daemons which receive audio samples from one or more sources,
mix them together, and send through the kernel to the appropriate hardware device.

This is no different from what applications do when mixing audio sources together.
The only difference is who the recipient of the output samples is.

Because the daemons are just userspace programs, multiple APIs can exist
on a platform. They just can't all be sending to the same hardware device
at the same time unless they are implemented by the same daemon, as often
is the case to maintain support for older APIs.

The story on Linux is that PulseAudio, an API and daemon, has
bad latency and so JACK thrives as an alternative among audiophiles.

For this library we're targeting consumers so we're targeting PulseAudio
on Linux, as well as PipeWire later which supposedly fixes the latency
issues and even implements the PulseAudio API for backwards compatibility.

# Notes

Notes as I learn for and plan for this library.

## Fading

It's important to have the ability to fade the volume of a sound and it serves as
a good example of a high-ish level use case that the library would do good to provide.

Fading has to happen within the API because spamming a 'set volume' call from a lower-priority
and lower frame rate game thread isn't going to work very well for numerous reasons.

Even if the game thread had the ability to act perfectly for spammming a set volume call,
there's currently a 'sendLock' and 'volumeLock' involved in setting the volume (gain) of a voice.
This has to be disruptive to both threads.

FAudio supports 'XACT' (SbAudio has removed it) and implements it with an additional audio thread,
matching the thread priority of the main audio thread. The XACT library has the concept
of fading, but FAudio implements it by spamming the 'set volume' call from this
additional audio thread. This is better than doing it from a game thread, but it should still bump
into the lock issue.

I have yet to learn how the XAPO feature works, and it might have a (non-easy?) way
to do fading from the audio thread.

## Locking

At the moment FAudio uses a 'sourceLock' mutex that's held during mixing of source
voices, and a 'submixLock' that's held during the mixing of submix voices.

These will not affect the game thread unless it is creating voices. And I suspect
games will usually cache and even pre-allocate a maximum number of them.

If a game doesn't do this then its game thread could get blocked for a measurable
amount of time. Let's just give a random number: 1ms worst case. // TODO: Measure this

One possible improvement would be to use a queue, lock free or not, to
prevent waiting on a lock while the audio thread does source and submix mixing.

The WebAudio API actually works differently. In it you can't even play a sound
twice from the same AudioBufferSourceNode, so you have to be able to create
them very quickly.
