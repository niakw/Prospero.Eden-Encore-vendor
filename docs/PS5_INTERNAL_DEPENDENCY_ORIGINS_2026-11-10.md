# Internal PS5 dependency source provenance

The three paths below are ordinary Git tracked files in vendor, not
external Git submodules. Do not assume PS5 device qualification.

| Path in vendor | Original project | Exact upstream SHA |
| --- | --- | --- |
| encore-active/PS5_MihawkGen | https://github.com/mihawk-99/PS5_MihawkGen | c57232a9f16bcb6df5fe07c9bb631518f2b1cc0e |
| encore-active/PS5_FSR3OpticalFlow | https://github.com/Swordpdf/ps5-framegen | 64362101757cdd6f7df9eeb984cb53f03f1fbfca |
| encore-active/libwebp | https://github.com/webmproject/libwebp | 4fa21912338357f89e4fd51cf2368325b59e9bd9 (v1.6.0) |

MihawkGen includes its GPL/MIT and model-distillation credits, and the
original network model with SHA-256
31ecc7d69af1e9defd62b9abad71bc3430cd8a0a4d2f4eafcabaafa48c34bf6b.
FSR3 Optical Flow carries upstream and AMD notices; libwebp retains
COPYING and PATENTS. libwebp was previously a v1.5.0 external gitlink
at a4d7a715337ded4451fec90ff8ce79728e04126c. Updating to 1.6.0
is a separate, unqualified version change limited to this branch.

The engine must continue to point at its existing vendor gitlink until
the new source paths, host tests, and security checks pass. This branch
never advances main or builds a native PS5 release.
