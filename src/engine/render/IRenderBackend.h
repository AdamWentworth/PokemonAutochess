#pragma once

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    virtual const char* backendId() const = 0;
    virtual void beginFrame(float r, float g, float b, float a) = 0;
    virtual void shutdown() = 0;
};
