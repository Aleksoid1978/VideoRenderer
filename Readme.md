# MPC Video Renderer

MPC Video Renderer is a free and open-source video renderer for DirectShow. The renderer can potentially work with any DirectShow player, but full support is available only in the MPC-BE. Recommended MPC-BE 1.6.5.164 (8e684d199) or newer.

## Key features:
- Shader video processor for NV12, YV12, YUY2, YV16, P010, P016, YV24, AYUV, P210, P216, Y410, Y416, RGB24/32/48/64, b48r, b64a, Y8, Y16.
- Supports DXVA2 and Direct3D 11 hardware decoding.
- Supports subtitle output in conjunction with MPC-BE.
- Automatic HDR to SDR conversion.
- Frame rotation (supported by MPC-BE).
- Chroma scaling algorithm for increasing the color component from 4:2:0 and 4:2:2 to 4:4:4.
- Dithering when the final color depth is reduced from 10/16 bits to 8 bits.
- Direct3D11 Video Processor with hardware deinterlacing for NV12, YUY2, P010 (+ RGB for Intel and AMD).
- Supports HDR10 data Passthrough (requires Windows 10 and an HDR10 supported display)

## Minimum system requirements

* An SSE2-capable CPU
* Windows 7¹ or newer
* DirectX 9.0c video card

¹For Windows 7, you must have D3DCompiler_47.dll file. It can be installed via update KB4019990.

## Recommended system requirements

* An SSE2-capable CPU
* Windows 8.1 or newer
* DirectX 10/11 video card

## License

MPC Video Renderer's code is licensed under [GPL v3].

## Links

Nightly builds - <https://yadi.sk/d/X0EVMKP4TcmnHQ>

Topic in MPC-BE forum (Russian) - <https://mpc-be.org/forum/index.php?topic=381>

MPC-BE - <https://sourceforge.net/projects/mpcbe/>

## Donate

<https://mpc-be.org/forum/index.php?topic=240>
