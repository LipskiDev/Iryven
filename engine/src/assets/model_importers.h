#pragma once

#include <filesystem>

#include <iryven/model.h>

namespace Iryven::Importers {

[[nodiscard]] ModelHandle ImportObj(const std::filesystem::path& path);
[[nodiscard]] ModelHandle ImportGltf(const std::filesystem::path& path);

} // namespace Iryven::Importers
