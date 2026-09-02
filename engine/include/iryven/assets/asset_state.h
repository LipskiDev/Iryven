#pragma once
#include <string>
#include "asset_handle.h"
#include <filesystem>

namespace Iryven {
	enum class LoadingState {
		Queued,
		LoadingCpu,
		WaitingGpu,
		Ready,
		Failed
	};

	enum class AssetType {
		Model,
		Texture,
		Material,
	};

	struct AssetState {
		AssetHandle asset;
		std::filesystem::path path;
		AssetType type = AssetType::Model;
		LoadingState state = LoadingState::Queued;
		std::string error;
	};
}
