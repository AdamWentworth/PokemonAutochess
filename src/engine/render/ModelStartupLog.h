// src/engine/render/ModelStartupLog.h
#pragma once

#include <iostream>

#ifndef PAC_VERBOSE_STARTUP
#define PAC_VERBOSE_STARTUP 0
#endif

// Verbose, opt-in startup logging (stdout)
#if PAC_VERBOSE_STARTUP
    #define STARTUP_LOG(msg) do { std::cout << msg << "\n"; } while(0)
#else
    #define STARTUP_LOG(msg) do {} while(0)
#endif

// Warnings that should be visible even when verbose startup logs are disabled (stderr)
#define STARTUP_WARN(msg) do { std::cerr << msg << "\n"; } while(0)
