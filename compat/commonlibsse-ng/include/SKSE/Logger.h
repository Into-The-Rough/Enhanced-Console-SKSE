#pragma once

#define SKSE_MAKE_NOOP_LOGGER(a_func)                                     \
	template <class... Args>                                               \
	struct [[maybe_unused]] a_func                                         \
	{                                                                      \
		a_func() = delete;                                                   \
		explicit a_func(spdlog::format_string_t<Args...>, Args&&...) {}      \
	};                                                                     \
	template <class... Args>                                               \
	a_func(spdlog::format_string_t<Args...>, Args&&...) -> a_func<Args...>;

namespace SKSE::log
{
	SKSE_MAKE_NOOP_LOGGER(trace);
	SKSE_MAKE_NOOP_LOGGER(debug);
	SKSE_MAKE_NOOP_LOGGER(info);
	SKSE_MAKE_NOOP_LOGGER(warn);
	SKSE_MAKE_NOOP_LOGGER(error);
	SKSE_MAKE_NOOP_LOGGER(critical);

	[[nodiscard]] std::optional<std::filesystem::path> log_directory();

	void add_papyrus_sink(std::regex a_filter);
	void remove_papyrus_sink();
	void init();
}

#undef SKSE_MAKE_NOOP_LOGGER
