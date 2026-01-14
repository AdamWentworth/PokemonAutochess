// src/engine/render/ModelStartupLog.h
#pragma once

#include <iostream>
#include <string>

#ifndef PAC_VERBOSE_STARTUP
#define PAC_VERBOSE_STARTUP 0
#endif

#if PAC_VERBOSE_STARTUP
    #define STARTUP_LOG(msg) do { std::cout << (msg) << "\n"; } while(0)
#else
    #define STARTUP_LOG(msg) do {} while(0)
#endif
