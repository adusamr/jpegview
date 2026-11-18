# JPEGView - Image Viewer and Editor

This is a fork of [sylikc/jpegview](https://github.com/sylikc/jpegview) that adds **display color profile support**. See [Display color profiles](#display-color-profiles) for the settings.

It also includes the changes made in sylikc/jpegview after its last release (1.3.46), which were not released there.

## Display color profiles

Set these options in `JPEGView.ini`:

| INI Option | Description |
| ---------- | ----------- |
| `DisplayColorProfile` | `auto` (default): use the profile assigned to the monitor in Windows color management. `none`: no display color management. Or the filepath of an ICC profile (`.icc`, `.icm`), absolute or relative to the JPEGView folder. |
| `UseEmbeddedColorProfiles` | `true` (default): use the ICC profiles embedded in images. This does not slow down loading images. The default value in sylikc/jpegview was `false`.|

Nothing is converted when the monitor uses the default sRGB profile, or when Windows color manages the monitor itself (HDR or "Automatically manage color for apps").

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

* 64-bit version: Windows 7 SP1 or later, 64-bit
* 32-bit version: Windows 7 SP1 or later

## What's New

* See what has changed in the [releases](https://github.com/adusamr/jpegview/releases)
* Or check the [CHANGELOG.txt](CHANGELOG.txt) to review new features in detail.

# Localization

By default, the language is auto-detected to match your Windows Locale.  All the text in the menus and user interface should show in your language.  To override the auto-detection, manually set `Language` option in `JPEGView.ini`

JPEGView is currently translated/localized to 28 languages:

| INI Option | Language |
| ---------- | -------- |
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
