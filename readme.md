# Hybrid Shadows 

Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.

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

## Hybrid Shadows

A sample to show off a way to accelerate raytraced shadows by using traditional shadow map techniques.

![Screenshot](screenshot.png)

## Build Instructions

### Prerequisites

To build Hybrid Shadows, you must first install the following tools:

- [CMake 3.16](https://cmake.org/download/)
- [Visual Studio 2019](https://visualstudio.microsoft.com/downloads/)
- [Windows 10 SDK 10.0.19041.0](https://developer.microsoft.com/en-us/windows/downloads/windows-10-sdk)

To run the sample a DXR 1.1 capable GPU is required.

To build the sample:

1) Clone the repo with its submodules:
    ```
    > git clone https://github.com/GPUOpen-Effects/Hybrid-Shadows.git --recurse-submodules
    ```

2) Generate the solution:
    ```
    > cd Hybrid-Shadows\build
    > GenerateSolutions.bat
    ```

3) Open the solution in the DX12 directory, compile and run.

