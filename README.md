# Moonlight Streaming Core Library

Moonlight-common-c contains the core GameStream client code shared between [Moonlight](https://moonlight-stream.org) clients, including [Moonlight PC](https://github.com/moonlight-stream/moonlight-qt), [Moonlight Android](https://github.com/moonlight-stream/moonlight-android), [Moonlight iOS](https://github.com/moonlight-stream/moonlight-ios), and [Moonlight Chrome](https://github.com/moonlight-stream/moonlight-chrome).

If you are implementing your own Moonlight game streaming client that can use a C library, you probably want the code here.

## Note to Developers

Moonlight-common-c requires the _specific_ version of ENet that is bundled as a submodule. This version has changes required for IPv6 compatibility and retransmission reliability, among other things. These are breaking API/ABI changes which make Moonlight-common-c incompatible with other versions of the ENet library. Attempting to runtime link to another libenet library will cause your client to crash when connecting to recent versions of GeForce Experience.

## Experimental PyroWave contract

`src/PyrowaveProtocol.h` and `src/PyrowaveProtocol.c` define the experimental
PyroWave wire contract. It provides bounds-checked packet framing, capability
intersection, and bounded frame reassembly. The [Frame Envelope specification](docs/pyrowave-frame-envelope.md)
documents header offsets, metadata flags, FEC, negotiation, and failure semantics.
The RTSP/depacketizer integration
is still opt-in: it is selected only when the application advertises the
experimental format, the server supplies matching version/capability fields,
and a compatible decoder is available. Protocol 2 (bitstream 2, payload 3)
supports SDR BT.709 YUV420 and static HDR10/PQ or HLG BT.2020, each with either
the negotiated limited or full YUV range. The selected session carries that
range in the existing `encoderCscMode`; the legacy color-range preference is
not changed. Legacy video formats are unchanged.
The depacketizer validates metadata TLVs before queuing codec bytes. It consumes
`HOST_PROCESSING_LATENCY`, skips unsupported optional types, and drops frames
with malformed or unsupported required metadata. Color contracts remain in the
codec sequence header; static HDR mastering metadata uses the HDR control channel.
The corresponding `pyrowave-protocol-golden-tests` and
`pyrowave-reassembly-golden-tests` targets are covered by CTest when
`BUILD_TESTING=ON`.
