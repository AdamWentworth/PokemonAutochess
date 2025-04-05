// UIManager.h

#pragma once
#include "../utils/Shader.h"

class UIManager {
public:
    // Call this at application startup.
    static void init();

    // Returns the UI shader used for drawing cards.
    static Shader* getCardShader();

    // Call this at application shutdown.
    static void shutdown();

private:
    static Shader* cardShader;
};
