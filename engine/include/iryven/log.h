#pragma once

#include <memory>
#include <spdlog/spdlog.h>
#include "spdlog/fmt/ostr.h"
#include <iryven/formatters.h>

namespace Iryven {
	class Log {
	public:
		static void Init();

		inline static std::shared_ptr<spdlog::logger>& GetCoreLogger() { return coreLogger_;  }
		inline static std::shared_ptr<spdlog::logger>& GetClientLogger() { return clientLogger_;  }

	private:
		static std::shared_ptr<spdlog::logger> coreLogger_;
		static std::shared_ptr<spdlog::logger> clientLogger_;
	};
}

#define IRYVEN_CORE_TRACE(...) ::Iryven::Log::GetCoreLogger()->trace(__VA_ARGS__)
#define IRYVEN_CORE_INFO(...) ::Iryven::Log::GetCoreLogger()->info(__VA_ARGS__)
#define IRYVEN_CORE_WARN(...) ::Iryven::Log::GetCoreLogger()->warn(__VA_ARGS__)
#define IRYVEN_CORE_ERROR(...) ::Iryven::Log::GetCoreLogger()->error(__VA_ARGS__)
#define IRYVEN_CORE_FATAL(...) ::Iryven::Log::GetCoreLogger()->critical(__VA_ARGS__)

#define IRYVEN_CLIENT_TRACE(...) ::Iryven::Log::GetClientLogger()->trace(__VA_ARGS__)
#define IRYVEN_CLIENT_INFO(...) ::Iryven::Log::GetClientLogger()->info(__VA_ARGS__)
#define IRYVEN_CLIENT_WARN(...) ::Iryven::Log::GetClientLogger()->warn(__VA_ARGS__)
#define IRYVEN_CLIENT_ERROR(...) ::Iryven::Log::GetClientLogger()->error(__VA_ARGS__)
#define IRYVEN_CLIENT_FATAL(...) ::Iryven::Log::GetClientLogger()->critical(__VA_ARGS__)