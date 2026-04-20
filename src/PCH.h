#pragma once

// CommonLibSSE-NG must be included BEFORE any direct Windows API include;
// it pulls in its own sanitized Windows wrappers.
#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>

// STL
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace logger = SKSE::log;
using namespace std::literals;
