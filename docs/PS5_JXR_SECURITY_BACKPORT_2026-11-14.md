# JXR security backport review — 2026-11-14

**Release blocker, no native or whole-codec safety claim.** The selected-image adapter from ProsperoGallery did not upgrade the legacy 42-file JXR source subset. Full-host compile/link succeeded on engine 03d6596 but only rejected five inputs before meaningful decoder processing. New upstream research identified the same source patterns as [oss-security, 2026-10-24](https://www.openwall.com/lists/oss-security/2026/10/24/1) and [Debian #1151921](https://bugs.debian.org/1151921). The maintained mircomir fork released 1.4.4 on 2026-10-09.

Apply the scoped upstream security changes below to the actual selected vendored sources; original bytes remain in Git at c339fd06f02e42e67d5416ae82194ba619b7ee68, existing BSD notices intact. Do not describe this partial backport as an upgrade to 1.4.4.

| Upstream reference | Actual selected source change |
| --- | --- |
| [cb97e6d](https://github.com/mircomir/jxrlib/commit/cb97e6debb5019cfa774f314c230629da05fc746) | JXRMeta count guard and size_t arithmetic |
| [d6e5f8d](https://github.com/mircomir/jxrlib/commit/d6e5f8d19d452d1191c329614077f0fecab933c6) | Check signature terminator before string operation; our old source also lacked the earlier terminator assignment |
| [debe47a](https://github.com/mircomir/jxrlib/commit/debe47a27c1ae37626fae9b8369db93e47d0aa19) | DecodeSignificantRun range guard |
| [ba86c04](https://github.com/mircomir/jxrlib/commit/ba86c041febb1472551681bae0d59a62e2e42072) | Check lookup bit-depth range |
| [190078d](https://github.com/mircomir/jxrlib/commit/190078d2c4d8fcc9e4660baf0f65c960fa95dd21) | Relevant null row-buffer guard only; remaining larger changes are not included |
| [7d2d462](https://github.com/mircomir/jxrlib/commit/7d2d462f6ca2d8395dde8b33883a9751299d6144) | Release-safe orientation validation |
| [mircomir master](https://github.com/mircomir/jxrlib) | Also backport explicit two-byte container signature comparison and three negative signed-shift / preprocessor token fixes (adapthuff, JXRGluePFC, strcodec.h) as observed in exact current source. |

The source changes address reported unsafe operations, not every reachable malformed stream path. Host gate compiles all 22 selected C units with ASan/UBSan, checks real overflow guard and benign invalid files, and attempts a genuine small generated SDR pixel roundtrip. Any sanitizer failure blocks qualification. Real PS5 HDR samples, native compiler/link, additional upstream fixes and broader malformed-image/fuzz regression coverage remain required. No release, no security alert dismissal.

## Actual sanitizer follow-up — file-stream lifetime

[CI 43555441954](https://github.com/niakw/Prospero.Eden-Encore-vendor/actions/runs/43555441954) found an 88-byte live WMPStream allocation on benign invalid-image rejection: CreateWS_File -> factory decoder initialization -> adapter. This is a real rejection-path defect; valid-image test did not execute yet. [Maintained upstream JXRGlue.c](https://github.com/mircomir/jxrlib/blob/master/jxrgluelib/JXRGlue.c) contains cleanup for failed Initialize; adapt its stream ownership transfer, but close unattached successful streams via their Close callback rather than only freeing their allocation. Also clean up failed fopen in CreateWS_File itself so the factory never tries to close a null FILE*. Extend the real sanitizer harness with repeated direct factory rejection and null-handle assertions. No suppression of LeakSanitizer.

## Valid-image sanitizer follow-up — bitstream shifts

[CI 43555675196](https://github.com/niakw/Prospero.Eden-Encore-vendor/actions/runs/43555675196) now passes the repeated malformed-file and allocation rejection suite, with no LeakSanitizer failure. The valid generated image instead reaches the original signed mask `~(-1 << cBits)` in strcodec.c:1004 and UBSan rejects the negative shift. Compared the exact implementation to [mircomir master](https://github.com/mircomir/jxrlib/blob/master/image/sys/strcodec.c), blob 973b39b6d2e6756f260bf43aeeaa94de408bd7ac; the same expression remains there. No matching upstream correction retrieved. Use an unsigned U32 mask for the 0..16 bit domain, and treat zero-bit writing and peeking as zero-width operations rather than shifting by 32. No sanitizer bypass. The actual image roundtrip is still required; additional decoder defects may remain.
