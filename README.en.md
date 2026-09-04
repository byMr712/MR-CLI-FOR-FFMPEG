# MR CLI FOR FFMPEG

> **Language:** English · [Русский](README.md)

## 📋 Description

MR CLI FOR FFMPEG is a convenient command-line wrapper for FFmpeg, providing an intuitive menu for performing a variety of operations with video and audio files without having to remember complex FFmpeg commands.

[![Version](https://img.shields.io/badge/version-1.1.3-green.svg)]()
[![Platform](https://img.shields.io/badge/platform-Windows-green.svg)]()
[![License](https://img.shields.io/badge/license-Apache%202.0-green.svg)](LICENSE)

## ✨ Features

### Video Operations
- **Format Conversion** — MP4, MKV, WEBM, AVI, MOV with support for H.264, H.265/HEVC, AV1, VP9 codecs
- **Video Trimming** — Trim/Cut with start and end time
- **Resolution Change** — from 360p to 4K with Lanczos filter
- **Speed Change** — speed up/slow down from 0.25x to 100x
- **Rotate and Flip** — 90°, 180°, 270°, mirror
- **Video Compression** — with customizable CRF and size savings visualization
- **Add Watermark** — with selectable position (corners, center)
- **Subtitle Insertion** — burn-in for SRT/ASS/SSA/VTT

### Audio Operations
- **Audio Extraction** — to MP3, M4A, WAV, FLAC, OGG
- **Video + Audio Merge** — combine tracks
- **Audio Removal** — create a silent version

### Other
- **File Joining** — combine multiple videos/audios
- **GIF Creation** — two-pass with quality palette
- **Frame Extraction** — single frame, interval, every Nth frame
- **File Information** — detailed analysis with FFprobe
- **File Comparison** — detailed side-by-side comparison table

### Batch Processing
- **Batch Video Compression** — compress all videos in a folder sequentially
- **Batch Audio Compression** — compress all audio files in a folder sequentially

## ⚙️ Settings

- Output Format (MP4, MKV, WEBM, MOV, AVI, MP3, M4A, WAV, FLAC, OGG) with preset and bitrate submenus
- Resolution, FPS
- Encoding Preset (ultrafast → veryslow) with arrow navigation and descriptions
- CRF (quality) with presets and custom input
- Audio Bitrate with presets and custom input
- Custom FFmpeg Args
- Hardware Acceleration: CPU (libx264), NVIDIA (NVENC), Intel (QSV), AMD (AMF), Hybrid (CPU+GPU), Reverse Hybrid (GPU+CPU)
- Auto-detection of GPU and CPU with display on the main screen
- File Overwriting, Metadata Preservation
- Codec prompt before processing (video/audio)
- Automatic FFmpeg installation
- Bilingual interface (English / Russian)

## 🛠️ Build

Requirements:
- Visual Studio 2022 (v143 toolset)
- Windows SDK 10.0
- C++17

```
msbuild mr-cli-ffmpeg.sln /p:Configuration=Release /p:Platform=x64
```

## 📦 Installation

When you first launch the program, it will automatically prompt you to download and install FFmpeg (~160 MB).

## 📄 License
1. [Apache 2.0] License (LICENSE) - use and modify freely!

## 🙏 Many thanks to
1. [FFmpeg](https://ffmpeg.org/)
