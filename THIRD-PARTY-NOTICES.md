# Third-Party Notices

This project incorporates code and pre-built libraries from third parties.
Their licences and copyright notices are reproduced below.

---

## 1. wdsp — NR algorithm source files

Files: `src/nr/anr.c`, `src/nr/anr.h`, `src/nr/emnr.c`, `src/nr/emnr.h`,
       `src/nr/comm.h`, `src/nr/calculus.c`, `src/nr/calculus.h`,
       `src/nr/zetaHat.c`, `src/nr/zetaHat.h`, `src/nr/FDnoiseIQ.c`,
       `src/nr/FDnoiseIQ.h`

Copyright (C) 2012, 2013, 2015, 2024, 2025 Warren Pratt, NR0V
<warren@wpratt.com> / <warren@pratt.one>

Original source: https://github.com/vu3rdd/wdsp (and related radio DSP
projects by Warren Pratt).

These files are distributed here under the same licence terms under which
they were received — GNU General Public License version 2 (or, at your
option, any later version). The full text of the GPL v2 is in the `LICENSE`
file at the root of this repository.

---

## 2. rnnr / sbnr — NR algorithm source files

Files: `src/nr/rnnr.c`, `src/nr/rnnr.h`, `src/nr/sbnr.c`, `src/nr/sbnr.h`

Copyright (C) 2000–2025 Original authors  
Copyright (C) 2020–2025 Richard Samphire, MW0LGE <mw0lge@grange-lane.co.uk>

Based on code and ideas from: https://github.com/vu3rdd/wdsp

These files are distributed here under the same licence terms under which
they were received — GNU General Public License version 2 (or, at your
option, any later version). The full text of the GPL v2 is in the `LICENSE`
file at the root of this repository.

---

## 3. FFTW3 — Fastest Fourier Transform in the West

Files: `libs/fftw/fftw3.h`, `libs/fftw/libfftw3-3.dll`,
       `libs/fftw/libfftw3-3.lib`, `libs/fftw/libfftw3f-3.dll`,
       `libs/fftw/libfftw3f-3.lib` (and related `.def`/`.exp` files)

Copyright (C) 2003, 2007–11 Matteo Frigo  
Copyright (C) 2003, 2007–11 Massachusetts Institute of Technology

Home page: https://www.fftw.org/

FFTW is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version. The full text of the GPL v2 is in the `LICENSE` file at the root
of this repository.

In addition, we kindly ask you to acknowledge FFTW and its authors in any
program that uses FFTW. See https://www.fftw.org/doc/ for details.

---

## 4. RNNoise

Files: `libs/rnnoise/rnnoise.h`, `libs/rnnoise/rnnoise.dll`,
       `libs/rnnoise/rnnoise.lib`, `libs/rnnoise/rnnoise_avx2.dll`,
       `libs/rnnoise/rnnoise_weights_large.bin`,
       `libs/rnnoise/rnnoise_weights_small.bin`

Copyright (C) 2008 Octasic Inc.  
Copyright (C) 2012 Mozilla Foundation  
Copyright (C) 2007–2008 CSIRO  
Copyright (C) 2007–2011 Skype Limited  
Copyright (C) 2003–2004 Mark Borgerding  
Copyright (C) 2017–2018 Jean-Marc Valin / Mozilla Foundation

Home page: https://gitlab.xiph.org/xiph/rnnoise

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.
- Neither the name of the copyright holder nor the names of its contributors
  may be used to endorse or promote products derived from this software
  without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

---

## 5. libspecbleach

Files: `libs/specbleach/specbleach_adenoiser.h`,
       `libs/specbleach/specbleach_denoiser.h`,
       `libs/specbleach/specbleach.dll`, `libs/specbleach/specbleach.lib`

Copyright (C) 2021 Luciano Dato <lucianodato@gmail.com>

Home page: https://github.com/lucianodato/libspecbleach

libspecbleach is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2 of the License, or (at your option)
any later version. The full text of the GPL v2 is in the `LICENSE` file at
the root of this repository.

---

## 6. CLAP — CLever Audio Plugin

Files: `include/clap/` (all header files)

Copyright (C) 2021 Alexandre Bique and contributors

Home page: https://github.com/free-audio/clap

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
