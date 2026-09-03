# MR CLI FOR FFMPEG v1.0.0

> **Language:** English · [Русский](README.md)

## 📋 Description

MR CLI FOR FFMPEG is a convenient command-line wrapper for FFmpeg, providing an intuitive menu for performing a variety of operations with video and audio files without having to remember complex FFmpeg commands.

## ✨ Features

### Video Operations
- **Format Conversion** — MP4, MKV, WEBM, AVI, MOV with support for H.264, H.265/HEVC, AV1, VP9 codecs
- **Video Trimming** — Trim/Cut with start and end time
- **Resolution Change** — from 360p to 4K with Lanczos filter
- **Speed ​​Change** — speed up/slow down from 0.25x to 100x
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

## ⚙️ Settings

- Output Format (10+ formats)
- Resolution, FPS
- Encoding Preset (Ultrafast → Veryslow)
- CRF (quality), audio bitrate
- Hardware Acceleration
- File Overwriting, Metadata Preservation
- Automatic Installation FFmpeg

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