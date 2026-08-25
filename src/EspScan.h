#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace eca::esp
{
	struct Record
	{
		std::uint32_t formID;
		std::string editorID;
		std::string type;
	};

	struct ScanResult
	{
		std::vector<Record> records;
		std::map<std::uint32_t, std::vector<std::uint16_t>> questStages;
	};

	constexpr std::uint32_t kBadPrefix = 0xFFFFFFFF;

	bool ReadMasters(const std::string& a_path, std::vector<std::string>& a_output,
		bool& a_isLight, std::string& a_error);
	bool ScanFile(const std::string& a_path, const std::vector<std::uint32_t>& a_prefixes,
		const std::set<std::string>& a_wantedTypes, ScanResult& a_output, std::string& a_error);
}
