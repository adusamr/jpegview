# JPEGView - Image Viewer and Editor

This is a fork of [sylikc/jpegview](https://github.com/sylikc/jpegview) that adds **display color profile support**.

It also includes all changes from [KrokusPokus/JPEGView_L](https://github.com/KrokusPokus/JPEGView_L) (linear-light resampling, CBZ/CB7 support, smooth panning and more) and the changes made in sylikc/jpegview after its last release (1.3.46), which were not released there, see below.

## Display color profiles

Display color profile support and embedded color profile support are enabled by default.

| JPEGView.ini Option | Description |
| ---------- | ----------- |
| `DisplayColorProfile` | `auto` (default): use the profile assigned to the monitor in Windows color management. `none`: no display color management. Or the filepath of an ICC profile (`.icc`, `.icm`), absolute or relative to the JPEGView folder. |
| `UseEmbeddedColorProfiles` | `true` (default): use the ICC profiles embedded in images. The default value in sylikc/jpegview was `false`.|

Nothing is converted when the monitor uses the default sRGB profile, or when Windows color manages the monitor itself (HDR or "Automatically manage color for apps").

## Changes from JPEGView_L

* Up- and downsamples images in linear space. This is beneficial for lineart, where lines and edges otherwise come out too dark and screentones can show moiree patterns that don't exist in the actual image. This is of course also a problem with photographs, where image detail of the zoomed down on screen image will come out too dark, and while professional image editing tools like Photoshop and GIMP have been using linear light scaling for a long time, very few image viewers exist that actually give correct output, sticking to a incorrect but faster method of calculation instead. JPEGView_L uses multi-threading and SSE/AVX2 extensions to maintain a high resampling speed (~70% of IrfanView) while not cutting any corners quality-wise.
More information: http://www.ericbrasseur.org/gamma.html
* The selection of available downsampling filters have been extended to offer the well understood 'Hermite', 'Mitchell', 'Catrom' for INI setting "DownSamplingFilter".
More information on how resampling filters work: http://legacy.imagemagick.org/Usage/filter/
* Support for CBZ and CB7 comic book archive format. Shoutout to [sdneon](https://github.com/sdneon/jpegview) who came up with the implementation
* Bookmark support for CBZ/CB7 files. Those archives are automatically opened on the page you last closed them on. Bookmarks are stored in "%AppData%\JPEGView\Bookmarks.ini"
* Custom zoom size for Book Mode as percentage of window height (new INI setting: BookModePageHeight)
* One instance per folder mode (INI setting: SingleInstance=PerFolder)
* Smooth scrolling with vsync for panning keys (new INI setting: SmoothPanning=true)
* Smart panning keys that combine scrolling and jumping to next/previous image based on zoom (new INI setting: SmartPanningKeys=true)
* Allows using the default file type icons on your system as the JPEGView window icon (new INI setting: TitleBarUseFileIcon=true)
* Checkerboard background (instead of solid color) mode for files with transparency channel (new INI setting: TransparencyMode=Checkerboard). Implementation again done by [sdneon](https://github.com/sdneon/jpegview) for his own fork.
* Improved caching. Keeps the 2 images before and after the currently visible in cache. (new INI setting: CacheRange=2)
* Settings default to a more viewer centric workflow. Just the image - no Bottom Panel, Navigation Panel or Zoom Navigator overlay by default. (Note: This fork uses the same settings file location as the upstream version, "%AppData%\JPEGView", so if an older setting file already exists there, it will use the older settings)

JPEGView_L requires 64-bit Windows 7 or higher and a CPU with SSE2 (AVX2 recommended).

## Description

JPEGView is a lean, fast and highly configurable image viewer/editor with a minimal GUI.

### Formats Supported

JPEGView has built-in support the following formats:

* Popular: JPEG, GIF
* Lossless: BMP, PNG, TIFF, PSD/PSB
* Web: WEBP, JXL, HEIF/HEIC, AVIF
* Other: QOI, TGA, WDP, HDP, JXR
* Camera RAW formats:
  * Adobe (DNG), Canon (CRW, CR2, CR3), Nikon (NEF, NRW), Sony (ARW, SR2)
  * Olympus (ORF), Panasonic (RW2), Fujifilm (RAF)
  * Sigma (X3F), Pentax (PEF), Minolta (MRW), Kodak (KDC, DCR)
  * A full list is available here: [LibRaw supported cameras](https://www.libraw.org/supported-cameras)

Many additional formats are supported by Windows Imaging Component (WIC)

### Basic Image Editor

Basic on-the-fly image processing is provided - allowing adjusting typical parameters:

* sharpness
* color balance
* rotation
* perspective
* contrast
* local under-exposure/over-exposure

### Other Features

* Small and fast, uses AVX2/SSE2 and up to 4 CPU cores
* High quality resampling filter, preserving sharpness of images
* Basic image processing tools can be applied realtime during viewing
* Movie/Slideshow mode - to play folder of JPEGs as movie


# Installation

## Releases

Releases are made on this fork's [GitHub Releases](https://github.com/adusamr/jpegview/releases) page. Each release includes:

* **Portable archive** - unzip and run
* **Windows Installer (MSI)** - for installs
* **Source code** - build it yourself

The package managers that offer JPEGView (WinGet, Scoop, Chocolatey, PortableApps, Npackd) install the upstream sylikc/jpegview version, not this fork.

## Portable

JPEGView _does not require installation_ to run.  Just **unzip, and run** either the 64-bit version, or the 32-bit version depending on which platform you're on.  It can save the settings to the extracted folder and run entirely portable.

## MSI Installer

For those who prefer to have JPEGView installed for All Users, a 32-bit/64-bit installer is available.

The installer replaces an installed official (sylikc) JPEGView. The MSI is not code signed.

## System Requirements

* Windows 7 SP1 or later, 64-bit only. 32-bit builds are no longer made, as the JPEGView_L code does not support them.
* [Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe) for JPEG XL, HEIF/HEIC, AVIF and modern camera RAW files (LibRaw).
  It is not included in the portable archive or the installer: the GPL license of JPEGView does not allow distributing it together with the program
  (see [extras/third_party/README.md](extras/third_party/README.md)). Many systems already have it; if not, JPEGView shows an error with the download link when opening these files.
  All other formats work without it, and with the default settings camera RAW files then use the built-in decoder, which supports fewer cameras.

## What's New

* See what has changed in the [releases](https://github.com/adusamr/jpegview/releases)
* Or check the [CHANGELOG.txt](CHANGELOG.txt) to review new features in detail.

# Localization

By default, the language is auto-detected to match your Windows Locale.  All the text in the menus and user interface should show in your language.  To override the auto-detection, manually set `Language` option in `JPEGView.ini`

JPEGView is currently translated/localized to 28 languages:

| INI Option | Language |
| ---------- | -------- |
| ar | Arabic (العربية) |
| be | Belarusian |
| bg | Bulgarian |
| cs | Czech |
| de | German |
| el | Greek, Modern |
| es-ar | Spanish (Argentina) |
| es | Spanish |
| eu | Basque |
| fi | Finnish |
| fr | French (Français) |
| hu | Hungarian |
| it | Italian |
| ja | Japanese (日本語) |
| ko | Korean (한국어) |
| pl | Polish |
| pt-br | Portuguese (Brazilian) |
| pt | Portuguese |
| ro | Romanian |
| ru | Russian (Русский) |
| sk | Slovak |
| sl | Slovenian (Slovenščina) |
| sr | Serbian (српски) |
| sv | Swedish |
| ta | Tamil |
| tr | Turkish (Türkçe) |
| uk | Ukrainian (Українська) |
| zh-tw | Chinese, Traditional (繁體中文) |
| zh | Chinese, Simplified (简体中文) |

See the upstream [Localization wiki page](https://github.com/sylikc/jpegview/wiki/Localization#localization-status) for the translation status of each language.

# Help / Documentation

The JPEGView documentation is a little out of date, but should still give a good summary of the features. All options are described in `JPEGView.ini`.

This [readme.html](https://htmlpreview.github.io/?https://github.com/adusamr/jpegview/blob/main/src/JPEGView/Config/readme.html) is part of the JPEGView package.

# License

JPEGView is licensed under the GNU General Public License v2, see [LICENSE.txt](LICENSE.txt).

# Brief History

JPEGView was created by [David Kleiner](https://sourceforge.net/projects/jpegview/). The SourceForge project was abandoned, with the last update on [2018-02-24 (1.0.37)](https://sourceforge.net/projects/jpegview/files/jpegview/).

In 2020, [sylikc](https://github.com/sylikc) continued the project on GitHub as [sylikc/jpegview](https://github.com/sylikc/jpegview), starting from a direct clone of the SourceForge SVN repository, so all previous commits and original author comments are retained. Its last release was 1.3.46 (2023-10-07).

This fork builds on sylikc/jpegview and adds display color profile support.

## Special Thanks
Special thanks to [qbnu](https://github.com/qbnu) for adding additional codec support!
* Animated WebP
* Animated PNG
* JPEG XL with animation support
* HEIF/HEIC/AVIF support
* QOI support
* ICC Profile support for WebP, JPEG XL, HEIF/HEIC, AVIF
* LibRaw support (all updated RAW formats, such as CR3)
* Photoshop PSD support

Thanks to all the _translators_ which keep JPEGView strings up-to-date in different languages!  See [CHANGELOG.txt](CHANGELOG.txt) to find credits for translators at each release!
