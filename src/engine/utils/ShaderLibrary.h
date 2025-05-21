// ShaderLibrary.h

#pragma once
#include "Shader.h"
#include <memory>
#include <string>
#include <unordered_map>

/*  Simple singleton cache: call ShaderLibrary::get(vPath,fPath)  
    and receive a shared_ptr to the compiled program.            */
class ShaderLibrary {
public:
    static std::shared_ptr<Shader> get(const std::string& vert,
                                       const std::string& frag);

    // optional: clear on shutdown
    static void clear();

private:
    static std::unordered_map<std::string, std::shared_ptr<Shader>> cache;
    static std::string makeKey(const std::string& v, const std::string& f);
};
