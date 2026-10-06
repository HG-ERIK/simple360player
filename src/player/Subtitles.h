#pragma once
#include <string>
#include <vector>
#include "FFPlayer.h"

// Subtitle text as it should be drawn: formatting tags removed, lines split on "\n".
std::wstring CleanSubtitle( const std::string& raw );

// Reads an .srt file. Non-UTF-8 files are read as Windows-1250 (Central European).
bool LoadSrt( const std::string& path, std::vector<SubtitleCue>& out );
