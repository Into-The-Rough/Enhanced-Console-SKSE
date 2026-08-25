#include "SKSE/Logger.h"

#include "REX/W32/OLE32.h"
#include "REX/W32/SHELL32.h"

namespace SKSE::log
{
	std::optional<std::filesystem::path> log_directory()
	{
		wchar_t* buffer{ nullptr };
		const auto result = REX::W32::SHGetKnownFolderPath(
			REX::W32::FOLDERID_Documents,
			REX::W32::KF_FLAG_DEFAULT,
			nullptr,
			std::addressof(buffer));
		std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> knownPath(
			buffer,
			REX::W32::CoTaskMemFree);
		if (!knownPath || result != 0) {
			return std::nullopt;
		}

		std::filesystem::path path = knownPath.get();
		path /= "My Games";
		path /= std::filesystem::exists("steam_api64.dll") ?
			"Skyrim Special Edition" : "Skyrim Special Edition GOG";
		path /= "SKSE";
		return path;
	}

	void add_papyrus_sink(std::regex)
	{}

	void remove_papyrus_sink()
	{}

	void init()
	{}
}
