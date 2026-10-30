# Technical sources

## Nintendo Switch 1

Nintendo's official hardware specifications document:

- custom NVIDIA Tegra processor;
- 1280×720 built-in display;
- TV output up to 1920×1080 at 60 frames per second.

Source:
https://www.nintendo.com/fr-fr/Hardware/Gamme-Nintendo-Switch/Nintendo-Switch/Nintendo-Switch-1148779.html

The general profiles use **1080p/60 as the platform output ceiling**, not as a claim that every Switch game internally renders at 1080p.

## Standard / original PlayStation 5

Sony's official PS5 technical specifications document:

- AMD Ryzen Zen 2 CPU, 8 cores / 16 threads, up to 3.5 GHz;
- AMD Radeon RDNA 2-based GPU, up to 2.23 GHz / 10.3 TFLOPS;
- 16 GB GDDR6;
- 448 GB/s memory bandwidth;
- 4K/120-compatible video output.

Source:
https://blog.fr.playstation.com/2020/03/18/communication-de-nouveaux-dtails-concernant-la-playstation-5-caractristiques-techniques-matrielles

The repository does **not** convert PS5/Switch raw TFLOPS into a direct resolution multiplier. Emulator/JIT/graphics-translation overhead makes that comparison unsuitable for profile selection.

## Encore / Eden settings surface

Encore currently exposes these relevant general/per-game video controls:

- Vulkan / OpenGL renderer;
- internal resolution scales from 0.25x through 4x;
- Bilinear / AMD FSR / Bicubic / Nearest filtering;
- None / FXAA / SMAA anti-aliasing;
- 1080p / 1440p / 2160p TV output;
- 60 / 120 Hz output;
- Docked / Handheld console mode.

Relevant Encore source files:

- `headless/settings_store.h`
- `headless/prosperoeden/pe/ui/video_presets.hpp`

Repository:
https://github.com/niakw/Prospero.Eden-Encore

## General profile derivation

For a 1080p-class source:

- 1x = 1920×1080
- 1.25x ≈ 2400×1350
- 1.5x = 2880×1620
- 2x = 3840×2160
- 3x = 5760×3240
- 4x = 7680×4320

Pixel cost grows with the square of the scale:

- 1x = 1.00×
- 1.25x = 1.5625×
- 1.5x = 2.25×
- 2x = 4.00×
- 3x = 9.00×
- 4x = 16.00×

This is why **2x / 2160p** is used as the best-case general quality ceiling rather than 3x or 4x.

## EA SPORTS FC on Switch 1

### EA SPORTS FC 24

Nintendo Life reports the Frostbite Switch version at:

- 1080p docked;
- 720p handheld;
- 30 FPS.

https://www.nintendolife.com/news/2023/09/ea-sports-fc-24-switch-frame-rate-and-resolution-revealed

https://www.nintendolife.com/news/2023/10/video-ea-sports-fc-24-graphics-comparison-shows-off-switch-performance-and-resolution

### EA SPORTS FC 25

Nintendo Life reports the Switch version remains at 30 FPS.

https://www.nintendolife.com/reviews/nintendo-switch/ea-sports-fc-25

### EA SPORTS FC 27

Nintendo lists the Nintendo Switch release dated 25 September 2026.

https://www.nintendo.com/fr-fr/Jeux/Jeux-Nintendo-Switch/EA-SPORTS-FC-27-3147152.html

No exact FC27 Switch 1 internal pixel count is asserted without a reliable direct measurement.
