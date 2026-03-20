# clap-nr

**WORK IN PROGRESS - NOT READY FOR USE**

This plugin is under active development and is not yet in a functional state.
It will load in a compatible host, but you should expect broken or silent
audio output and likely crashes. This is known. It is not a bug report
waiting to happen - it is the current state of an unfinished project.

Please do not open issues reporting broken audio, crashes, or missing
functionality. We are aware. Issue reports of this kind will be closed
without response until a formal testing release has been announced.

---

**clap-nr** is a [CLAP](https://cleveraudio.org/) audio plugin that brings
high-quality DSP noise-reduction to any host application that supports the
CLAP plugin format.

**This is predominantly derivative work.** The noise-reduction algorithms
that form the core of this plugin were written by others - specifically
Warren Pratt (NR0V) and Richard Samphire (MW0LGE) - and are reproduced here
under the terms of the GNU General Public License. Our contribution is the
CLAP plugin wrapper, the audio integration layer, and the Win32 user
interface. We make no claim of authorship over the DSP algorithms themselves,
and this repository would not exist without the work of those individuals.
Full attribution is given in the [Special Thanks](#special-thanks) section
and in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

The plugin was written primarily to support
[Station Master Pro](https://www.g5stu.co.uk/), an advanced amateur radio
application created by Stuart E. Green (G5STU). Station Master Pro handles
digital audio processing for ham radio operators and has native CLAP plugin
support, making it possible to run these noise-reduction algorithms as a
first-class audio effect within the application.

The broader goal is to make the noise-reduction technology found in
established amateur radio DSP stacks available to the wider amateur radio
community through an open, vendor-neutral plugin standard - without
requiring users to run any specific radio application to benefit from it.

**This plugin is not designed to run standalone.** It must be loaded by a
host application that supports the CLAP plugin format. If you are new to
audio plugins, see the [CLAP host compatibility list](https://cleveraudio.org/)
for suitable host applications.

---

## A note on finances

This repository and plugin are provided free of charge with no commercial
intent whatsoever. No donations to this project are sought or accepted.

The real work here was done by the authors of the underlying DSP libraries
and algorithms. If you find this plugin useful and want to show appreciation,
please direct any support to those projects directly. See the
[Special Thanks](#special-thanks) section below for links.

---

## Noise-reduction modes

| NR Mode | Algorithm | Description |
|---------|-----------|-------------|
| 0 | Off | Bypass |
| 1 | NR1 - ANR | Adaptive LMS noise reduction |
| 2 | NR2 - EMNR | Spectral MMSE with ML gain estimation |
| 3 | NR3 - RNNR | RNNoise recurrent neural network |
| 4 | NR4 - SBNR | libspecbleach adaptive denoiser |

| Parameter | Range | Notes |
|-----------|-------|-------|
| NR Mode | 0-4 | Selects algorithm; 0 = bypass |
| NR4 Reduction | 0-20 dB | Only active when NR Mode = 4 |

---

## Platform support

Currently **Windows x64 only** (MSVC or MinGW-w64).

macOS and Linux are not currently supported. Pull requests that add support
for either platform are welcome, provided they do not break the existing
Windows build or introduce regressions. Please keep platform-specific code
well-isolated.

---

## Building on Windows

### Prerequisites

- CMake 3.20 or later
- Visual Studio 2022 (MSVC, x64) **or** MinGW-w64 (x64)

### Step 1 - Build

```
cmake -B build -S . -A x64
cmake --build build --config Release
```

The output is `build\Release\clap-nr.clap`.

### Step 2 - Install

Copy `clap-nr.clap` and the three runtime DLLs to your CLAP plugin folder:

```
%COMMONPROGRAMFILES%\CLAP\
```

The required DLLs are already present under `libs/` and should be copied
alongside the plugin:

- `libfftw3-3.dll`
- `rnnoise.dll`
- `specbleach.dll`

---

## Project layout

```
clap-nr/
├── CMakeLists.txt
├── LICENSE                  <- GNU GPL v2
├── THIRD-PARTY-NOTICES.md   <- upstream copyright notices
├── README.md
├── include/
│   └── clap/                <- CLAP SDK headers (vendored, MIT licence)
├── libs/
│   ├── fftw/                <- fftw3.h + .dll/.lib
│   ├── rnnoise/             <- rnnoise.h + .dll/.lib + weights
│   └── specbleach/          <- specbleach_*.h + .dll/.lib
└── src/
    ├── clap_nr.c            <- CLAP plugin entry point and audio processing
    ├── gui_win32.c          <- Win32 parameter UI
    ├── gui.h
    └── nr/                  <- DSP noise-reduction algorithm sources
        ├── anr.c/h          (NR1 - Adaptive LMS)
        ├── emnr.c/h         (NR2 - Spectral MMSE / ML)
        ├── rnnr.c/h         (NR3 - RNNoise neural net)
        ├── sbnr.c/h         (NR4 - libspecbleach)
        ├── calculus.c/h     <- lookup tables for EMNR
        ├── zetaHat.c/h
        ├── FDnoiseIQ.c/h
        └── comm.h
```

---

## Licence

This project is distributed under the **GNU General Public License v2**
(or, at your option, any later version). See [LICENSE](LICENSE) for the
full text.

This licence is inherited from the upstream DSP sources. All third-party
copyright notices are preserved in full in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

---

## Special thanks

clap-nr would not exist without the following people and projects.
Please visit their repositories, star their work, and consider supporting
them directly - they are the ones who wrote the algorithms that make this
plugin useful.

**Warren Pratt, NR0V** - `anr`, `emnr`, `comm`, `calculus`, `zetaHat`, `FDnoiseIQ`
The core DSP noise-reduction algorithms (ANR and EMNR) originate from
Warren's wdsp library, a high-quality DSP stack written for amateur radio
software-defined radio applications.
- https://github.com/vu3rdd/wdsp

**Richard Samphire, MW0LGE** - `rnnr`, `sbnr`
The RNNoise and libspecbleach integration (NR3 and NR4) was authored by
Richard as part of a larger radio application. His work brought neural-network
and spectral noise reduction into the amateur radio DSP world.
- https://github.com/ramdor/Thetis

**Jean-Marc Valin and the Xiph.Org Foundation** - RNNoise
The recurrent neural network noise suppression library used by NR3.
- https://gitlab.xiph.org/xiph/rnnoise

**Luciano Dato** - libspecbleach
The adaptive spectral noise reduction library used by NR4.
- https://github.com/lucianodato/libspecbleach

**Matteo Frigo and MIT** - FFTW3
The fast Fourier transform library that underpins the frequency-domain
processing in EMNR.
- https://www.fftw.org/

**Alexandre Bique and contributors** - CLAP
The open plugin standard that makes it possible to distribute this work
in a host-neutral, vendor-neutral format.
- https://github.com/free-audio/clap

---

The information in this README is accurate to the best of our knowledge and
understanding at the time of writing. If you spot anything incorrect -
whether a licence detail, an author attribution, a build instruction, or
anything else - please feel free to submit a pull request to correct it.
Apologies in advance for any errors.
