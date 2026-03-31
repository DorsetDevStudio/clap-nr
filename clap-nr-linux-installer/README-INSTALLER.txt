clap-nr - Noise Reduction Plugin for CLAP
==========================================

This is a standalone Linux installer package that requires no additional
dependencies beyond what's typically available on any Linux system with a GUI.

INSTALLATION
------------

1. Extract this archive to any location
2. Open a terminal in this directory
3. Run the installer:

   ./install.sh

   This installs to ~/.clap/ (user-level, recommended)

   For system-wide installation (all users):

   sudo ./install.sh --system

UNINSTALLATION
--------------

Run the uninstall script from this directory:

   ./uninstall.sh

   Or for system-wide removal:

   sudo ./uninstall.sh --system

SYSTEM REQUIREMENTS
-------------------

This plugin should work on any modern Linux distribution with:
- glibc 2.31 or newer (Ubuntu 20.04+, Debian 11+, Fedora 30+, etc.)
- OpenGL/Mesa (for GUI)
- GLFW3 (for GUI)

These are standard libraries available on virtually all Linux desktop systems.

The plugin uses statically-linked builds of:
- FFTW3 (Fast Fourier Transform library)
- RNNoise (Xiph.org's neural network denoiser)
- libspecbleach (Spectral noise reduction)

So you don't need to install these separately.

USAGE
-----

After installation, load "clap-nr" in your CLAP-compatible DAW or audio host.

The plugin provides 5 noise reduction algorithms - see README.md for details.

SUPPORT
-------

For issues, visit: https://github.com/yourusername/clap-nr
Website: https://clapnr.com

