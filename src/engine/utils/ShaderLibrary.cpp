// ShaderLibrary.cpp

#include "ShaderLibrary.h"

std::unordered_map<std::string, std::shared_ptr<Shader>> ShaderLibrary::cache;

std::string ShaderLibrary::makeKey(const std::string& v,const std::string& f){
    return v + "::" + f;
}

std::shared_ptr<Shader> ShaderLibrary::get(const std::string& vert,
                                           const std::string& frag)
{
    std::string key = makeKey(vert, frag);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    auto shader = std::make_shared<Shader>(vert.c_str(), frag.c_str());
    cache[key]  = shader;
    return shader;
}

void ShaderLibrary::clear() { cache.clear(); }
