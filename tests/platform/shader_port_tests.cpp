#include <shader/shader_compiler.h>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <fstream>
#include <sstream>

using namespace Velos;
using namespace Velos::RHI;

int main() {
  try {
    const std::map<std::string, ShaderStage> stages{
        {"VERTEX", ShaderStage::Vertex},
        {"FRAGMENT", ShaderStage::Fragment},
        {"COMPUTE", ShaderStage::Compute},
        {"TASK", ShaderStage::Task},
        {"MESH", ShaderStage::Mesh}};
    unsigned count = 0;
    for (const auto &file :
         std::filesystem::directory_iterator("assets/shaders/internal")) {
      const auto path = file.path();
      if (path.extension() != ".hlsl")
        continue;
      std::ifstream source(path);
      std::string header;
      std::getline(source, header);
      const std::string marker = "// IRYVEN_SHADER_STAGES:";
      if (!header.starts_with(marker))
        continue;
      std::istringstream declaredStages(header.substr(marker.size()));
      std::string stageName;
      while (declaredStages >> stageName) {
        const auto found = stages.find(stageName);
        if (found == stages.end())
          throw std::runtime_error("Unknown shader stage: " + stageName);
        auto dxil = ShaderCompiler::CompileFile(
            {.path = path.string(),
             .stage = found->second,
             .language = ShaderSourceLanguage::HLSL,
             .outputFormat = ShaderBinaryFormat::Dxil});
        auto spirv = ShaderCompiler::CompileFile(
            {.path = path.string(),
             .stage = found->second,
             .language = ShaderSourceLanguage::HLSL,
             .outputFormat = ShaderBinaryFormat::Spirv});
        if (dxil.dxil.empty() || spirv.spirv.empty())
          throw std::runtime_error("Missing shader output");
        const auto merged = ShaderCompiler::MergeShaderReflection(
            std::span(&dxil.reflection, 1));
        for (const auto &binding : merged.resources) {
          bool match = false;
          for (const auto &other : spirv.reflection.resources)
            if (binding.set == other.set && binding.binding == other.binding &&
                (binding.type == other.type ||
                 (binding.type == ShaderResourceType::SampledImage &&
                  other.type == ShaderResourceType::CombinedImageSampler))) {
              if ((!binding.runtimeArray &&
                   binding.arraySize != other.arraySize) ||
                  binding.runtimeArray != other.runtimeArray)
                throw std::runtime_error(path.string() +
                                         ": descriptor array ABI differs");
              match = true;
              break;
            }
          if (!match)
            throw std::runtime_error(
                path.string() + ": descriptor binding ABI differs at set " +
                std::to_string(binding.set) + " binding " +
                std::to_string(binding.binding));
        }
        for (const auto &range : dxil.reflection.pushConstants) {
          bool match = false;
          for (const auto &other : spirv.reflection.pushConstants)
            // Native constant buffers round their final size to 16 bytes;
            // Vulkan push blocks expose the final member's actual end.
            if (range.offset == other.offset &&
                range.size == ((other.size + 15u) & ~15u))
              match = true;
          if (!match)
            throw std::runtime_error(path.string() +
                                     ": push-constant ABI differs");
        }
        std::cout << "PASS " << path.filename().string() << " " << stageName
                  << " DXIL/SPIR-V and reflected ABI\n";
        ++count;
      }
    }
    if (count != 23)
      throw std::runtime_error("Expected all 23 engine shader stages");
    for (const auto stage : {ShaderStage::Vertex, ShaderStage::Fragment}) {
      for (const auto format :
           {ShaderBinaryFormat::Dxil, ShaderBinaryFormat::Spirv}) {
        auto sandbox = ShaderCompiler::CompileFile(
            {.path = "sandbox/basic.hlsl",
             .stage = stage,
             .entryPoint = stage == ShaderStage::Vertex ? "VSmain" : "PSmain",
             .language = ShaderSourceLanguage::HLSL,
             .outputFormat = format});
        if (sandbox.GetBytecode().empty())
          throw std::runtime_error("Missing Sandbox shader output");
      }
    }
    std::cout << "PASS Sandbox vertex/pixel HLSL for both backends\n";
    std::cout << "PASS all " << count << " shader stages\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
