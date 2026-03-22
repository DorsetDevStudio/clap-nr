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
CLAP plugin wrapper, the audio integration layer, and the cross-platform
Dear ImGui user interface. We make no claim of authorship over the DSP algorithms themselves,
and this repository would not exist without the work of those individuals.
Full attribution is given in the [Special Thanks](#special-thanks) section
and in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

The plugin was written primarily to support
[Station Master Pro](https://www.stationmasterpro.com/), an advanced amateur radio
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

| Platform | Architecture | Status |
|----------|-------------|--------|
| Windows | x64 | Supported |
| macOS | Apple Silicon (arm64) | Supported |
| Linux | x64 | Supported |

---

## Getting, building, and installing

Choose the section for your platform below. Each section walks you through
everything from scratch. You do not need any prior programming experience.

- [Windows](#windows)
- [macOS](#macos)
- [Linux](#linux)

---

<a name="windows"></a>
## Windows

### Part 1 - Install the required tools (one time only)

You only need to do this once. If you already have Git, CMake, and Visual
Studio 2022 installed, skip to Part 2.

**Step 1 - Install Git**

Git is the tool used to download (clone) this repository from GitHub.

1. Go to https://git-scm.com/download/win and download the installer.
2. Run the installer. The default options are fine - just click Next through
   each screen and then Install.
3. When it finishes, close the installer.

**Step 2 - Install CMake**

CMake is the tool that prepares the build system before compiling.

1. Go to https://cmake.org/download/ and download the Windows x64 installer
   (the file ending in `-windows-x86_64.msi`).
2. Run the installer.
3. On the "Install Options" screen, select **Add CMake to the system PATH for
   all users**. This is important.
4. Click Next through the remaining screens and then Install.

**Step 3 - Install Visual Studio 2022 Community**

Visual Studio is the compiler that turns the source code into the plugin.
The Community edition is free.

1. Go to https://visualstudio.microsoft.com/vs/community/ and click
   **Download Visual Studio**.
2. Run the installer. When it asks what to install, tick the workload called
   **Desktop development with C++** and leave everything else as-is.
3. Click Install. This may take a while depending on your internet speed.

---

### Part 2 - Get the source code

1. Open **Command Prompt**. You can find it by pressing the Windows key,
   typing `cmd`, and pressing Enter.
2. Decide where you want to put the project folder. For example, if you want
   it on your Desktop, type:
   ```
   cd %USERPROFILE%\Desktop
   ```
3. Type the following command and press Enter to download the repository:
   ```
   git clone https://github.com/DorsetDevStudio/clap-nr.git
   ```
4. Once it finishes, a folder called `clap-nr` will appear in your chosen
   location. Type the following to enter it:
   ```
   cd clap-nr
   ```

---

### Part 3 - Build the plugin

1. In the same Command Prompt window (still inside the `clap-nr` folder),
   type the following and press Enter:
   ```
   build-win.bat
   ```
2. You will see a lot of text scroll past. This is normal. Wait for it to
   finish. It should end with something like `Build succeeded`.
3. When it is done, the compiled plugin file will be at:
   ```
   build\Release\clap-nr.clap
   ```

If you see any error messages, double-check that Visual Studio 2022 is
installed with the **Desktop development with C++** workload (Part 1, Step 3)
and that CMake was added to the PATH (Part 1, Step 2).

---

### Part 4 - Install the plugin

The install script copies the plugin and its required support files to the
standard Windows CLAP plugin folder so your host application can find it.

**You must run this as Administrator.**

1. Open **File Explorer** and navigate to the `clap-nr` folder.
2. Right-click on **install-win.bat** and choose **Run as administrator**.
3. If Windows asks "Do you want to allow this app to make changes to your
   device?", click **Yes**.
4. A Command Prompt window will open and run the install. When it finishes
   successfully you will see:
   ```
   Installed to C:\Program Files\Common Files\CLAP
   Press any key to continue . . .
   ```
5. Press any key to close the window.

The plugin is now installed. Open your CLAP-compatible host application
(for example, Station Master Pro) and it should appear in the plugin list.
You may need to trigger a plugin rescan inside the host.

---

### Part 5 - Uninstall the plugin

If you want to remove the plugin from your system:

**You must run as Administrator.**

1. Open **File Explorer** and navigate to the `clap-nr` folder.
2. Right-click on **uninstall.bat** and choose **Run as administrator**.
3. If Windows asks "Do you want to allow this app to make changes to your
   device?", click **Yes**.
4. A Command Prompt window will open. When it finishes you will see:
   ```
   Uninstalled from C:\Program Files\Common Files\CLAP
   Press any key to continue . . .
   ```
5. Press any key to close the window.

**Note:** If either script reports that a file is locked, it means your CLAP
host application is still running and has the plugin loaded. Close the host
application first, then run the script again.

---

<a name="macos"></a>
## macOS

The macOS build requires Apple Silicon (arm64). An Intel build is not
currently provided.

---

### Prerequisites

- Xcode Command Line Tools (includes `clang` and `make`)
- CMake 3.20 or later
- Homebrew (recommended for installing CMake)

**Install Xcode Command Line Tools:**
```
xcode-select --install
```

**Install CMake via Homebrew:**
```
brew install cmake
```

---

### Part 1 - Get the source code

1. Open **Terminal** (Finder > Applications > Utilities > Terminal).
2. Navigate to where you want to put the project:
   ```
   cd ~/Desktop
   ```
3. Clone the repository:
   ```
   git clone https://github.com/DorsetDevStudio/clap-nr.git
   cd clap-nr
   ```

---

### Part 2 - Build the plugin

```
bash build-mac.sh
```

The compiled plugin will be at `build-mac/clap-nr.clap`.

---

### Part 3 - Install the plugin

```
bash install-mac.sh
```

This copies the plugin to `~/Library/Audio/Plug-Ins/CLAP/` (user install,
no administrator password required).

The plugin is now installed. Open your CLAP-compatible host application
and it should appear in the plugin list. You may need to trigger a plugin
rescan inside the host.

---

<a name="linux"></a>
## Linux

The Linux build produces an x86_64 plugin.

---

### Prerequisites

You will need the following packages. On Debian/Ubuntu:
```
sudo apt install build-essential cmake git
```
On Fedora/RHEL:
```
sudo dnf install gcc gcc-c++ cmake git
```

---

### Part 1 - Get the source code

1. Open a terminal.
2. Navigate to where you want to put the project:
   ```
   cd ~/Desktop
   ```
3. Clone the repository:
   ```
   git clone https://github.com/DorsetDevStudio/clap-nr.git
   cd clap-nr
   ```

---

### Part 2 - Build the plugin

```
bash build-linux.sh
```

The compiled plugin will be at `build-linux/clap-nr.clap`.

---

### Part 3 - Install the plugin

**User install** (no root required - installs to `~/.clap/`):
```
bash install-linux.sh
```

**System-wide install** (requires sudo - installs to `/usr/lib/clap/`):
```
bash install-linux.sh --system
```

The plugin is now installed. Open your CLAP-compatible host application
and it should appear in the plugin list. You may need to trigger a plugin
rescan inside the host.

---

## Project layout

```
clap-nr/
├-- CMakeLists.txt
├-- LICENSE                  <- GNU GPL v2
├-- THIRD-PARTY-NOTICES.md   <- upstream copyright notices
├-- README.md
├-- build-win.bat            <- Windows: configure and build
├-- install-win.bat          <- Windows: install to %CommonProgramFiles%\CLAP  (run as Admin)
├-- uninstall.bat            <- Windows: uninstall  (run as Admin)
├-- build-mac.sh             <- macOS (arm64): configure and build
├-- install-mac.sh           <- macOS: install to ~/Library/Audio/Plug-Ins/CLAP/
├-- build-linux.sh           <- Linux (x64): configure and build
├-- install-linux.sh         <- Linux: install to ~/.clap/  or  /usr/lib/clap/
├-- include/
│   └-- clap/                <- CLAP SDK headers (vendored, MIT licence)
├-- libs/
│   ├-- fftw/                <- fftw3.h + .dll/.lib
│   ├-- rnnoise/             <- rnnoise.h + .dll/.lib + weights
│   └-- specbleach/          <- specbleach_*.h + .dll/.lib
└-- src/
    ├-- clap_nr.c            <- CLAP plugin entry point and audio processing
    ├-- gui_imgui.cpp        <- Cross-platform Dear ImGui UI
    ├-- gui.h
    └-- nr/                  <- DSP noise-reduction algorithm sources
        ├-- anr.c/h          (NR1 - Adaptive LMS)
        ├-- emnr.c/h         (NR2 - Spectral MMSE / ML)
        ├-- rnnr.c/h         (NR3 - RNNoise neural net)
        ├-- sbnr.c/h         (NR4 - libspecbleach)
        ├-- calculus.c/h     <- lookup tables for EMNR
        ├-- zetaHat.c/h
        ├-- FDnoiseIQ.c/h
        └-- comm.h
```

---

## Licence

This project is distributed under the **GNU General Public License v2**.
See [LICENSE](LICENSE) for the full text.

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
