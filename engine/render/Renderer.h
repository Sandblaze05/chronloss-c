#pragma once

class Renderer {
public:
    Renderer() = default;
    ~Renderer() = default;

    void beginFrame();
    void endFrame();
};
