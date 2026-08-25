#include "pch.h"

#include "ConsoleUI.h"
#include "Engine.h"

namespace
{
	//commonlibsse-ng supplies the cross-layout bit; skse 2.3 adds bit 1 for address library v5.
	constexpr auto kCrossRuntimeAddressLibraryV5 = static_cast<SKSE::StructCompatibility>(
		static_cast<std::uint32_t>(SKSE::StructCompatibility::Independent) | (1u << 1));

	void OnSKSEMessage(SKSE::MessagingInterface::Message* a_message)
	{
		if (!a_message) {
			return;
		}

		if (a_message->type == SKSE::MessagingInterface::kDataLoaded) {
			eca::engine::OnDataLoaded();
			eca::consoleui::Register();
		}
	}
}

using namespace std::literals;

SKSEPluginInfo(
	.Version = { 0, 1, 0, 0 },
	.Name = "EnhancedConsoleSSE"sv,
	.Author = "IntoTheRough"sv,
	.StructCompatibility = kCrossRuntimeAddressLibraryV5,
	.RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary,
	.MinimumSKSEVersion = { 0, 0, 0, 0 })

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
#ifndef NDEBUG
	spdlog::set_level(spdlog::level::debug);
	spdlog::flush_on(spdlog::level::debug);
#endif
	SKSE::log::info("runtime {}", a_skse->RuntimeVersion());

	const auto* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnSKSEMessage)) {
		SKSE::log::error("failed to register the SKSE message listener");
		return false;
	}

	return true;
}
