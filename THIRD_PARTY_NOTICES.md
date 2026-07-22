# Third-party notices

## allpowers-ble

Protocol information and communication logic in this project are derived from:

- Project: `allpowers-ble`
- Author: James Johnston / `madninjaskillz`
- Repository: <https://github.com/madninjaskillz/allpowers-ble>
- License: MIT

The upstream project uses the common status parser and AC/DC/light command encoder that this ESPHome
component ports to C++.

The original license notice is reproduced below.

```text
MIT License

Copyright (c) 2023 James Johnston

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## allpowers-companion

The connection-health design was informed by the publicly documented behavior and observed status-request
command in:

- Project: `allpowers-companion`
- Author: `R0b0To`
- Repository: <https://github.com/R0b0To/allpowers-companion>
- License: GPL-3.0

No source code from `allpowers-companion` is incorporated into this MIT project. The ESPHome keepalive and
watchdog were independently implemented in C++ using ESPHome's native BLE client lifecycle. This notice
records the design reference and the license boundary; it does not relicense this project.
