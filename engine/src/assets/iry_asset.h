#pragma once

#include <filesystem>
#include <iryven/model.h>

namespace Iryven::IryAsset {

void WriteModel(const std::filesystem::path& path, const Model& model);
[[nodiscard]] ModelHandle ReadModel(const std::filesystem::path& path);

}
