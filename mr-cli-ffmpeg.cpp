// ============================================================================
//  MR CLI FOR FFMPEG — Standalone Windows CLI frontend for FFmpeg
//  Licensed under the Apache License, Version 2.0.
// ============================================================================

#include "src/app/Application.hpp"

int main() {
    return mr::app::Application::instance().run();
}
