# JPEG XR decoder source and Eden PS5 integration

The `sources/jxrlib/` directory is an unmodified copy of the JPEG XR
codec sources bundled by [ProsperoGallery](https://github.com/AmineWallah/ProsperoGallery)
at git commit `ff728c83746d045fc3611e511ac2acb4a2b43e38`.
All 42 files are imported without source modifications, including
`sources/jxrlib/LICENSE` (BSD terms). The canonical JPEG XR
implementation comes from Microsoft. This is a third-party dependency, not
an Encore source override. No binary artifacts are committed.

`encore-active/ps5-jxr/jxr_decoder.c` is a modified adaptation of
ProsperoGallery's `src/jxr.c` at that same commit; it uses the
original HDR/scRGB/PQ tone mapping logic and decodes to 8-bit SDR RGB.
It is GPL-3.0-or-later, kept separately from the unmodified BSD codec.
Unlike ProsperoGallery, the Encore adapter emits one bounded **WebP** via
the pinned libwebp and does not enumerate or automatically convert
screenshots. Caller provides a *single user-selected* validated JXR file
and a private temporary cache path, then atomically publishes the result.

Source integration conditions:

- JXR inspection/listing must do only filesystem metadata checks; no
  JPEG XR decode, bulk conversion, or conversion while scrolling.
- The launcher resolves the selected capture on a detached worker; encoded
  size and decode dimensions are bounded, originals remain read-only.
- Only one selected JXR is decoded, resulting in at most a 1920x1080 SDR
  WebP cached under Encore data with atomic publication.
- Source provenance, source size, image dimensions and permission failures
  are validated before opening a codec; errors show an unavailable selected
  wallpaper without crashing the launcher.
- Native PS5 compilation, address-sanitizer behavior, malformed codec inputs,
  on-console image quality, HDR tone mapping and filesystem permissions are
  **not verified yet**. No PS5 build before NS1+NS2 corrections as requested.
