#include "EspScan.h"

#include "vendor/miniz.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace eca::esp
{
	namespace
	{
		struct RecordHeader
		{
			char type[4];
			std::uint32_t dataSize;
			std::uint32_t flags;
			std::uint32_t formID;
			std::uint32_t versionControl;
			std::uint16_t version;
			std::uint16_t unknown;
		};
		static_assert(sizeof(RecordHeader) == 24);

		constexpr std::uint32_t kCompressed = 0x00040000;
		constexpr std::uint32_t kLightFlag = 0x200;
		constexpr std::uint32_t kMaximumDecompressedSize = 64u * 1024u * 1024u;

		bool ReadHeader(std::ifstream& a_file, RecordHeader& a_header)
		{
			return static_cast<bool>(a_file.read(reinterpret_cast<char*>(&a_header), sizeof(a_header)));
		}

		bool Signature(const char (&a_type)[4], const char* a_value)
		{
			return std::memcmp(a_type, a_value, 4) == 0;
		}

		void WalkSubrecords(const unsigned char* a_data, std::size_t a_size, bool a_isQuest,
			std::string& a_editorID, std::vector<std::uint16_t>& a_stages)
		{
			std::size_t position = 0;
			while (position + 6 <= a_size) {
				const char* signature = reinterpret_cast<const char*>(a_data + position);
				std::uint16_t size = 0;
				std::memcpy(&size, a_data + position + 4, sizeof(size));
				position += 6;
				if (position + size > a_size) {
					return;
				}
				if (std::memcmp(signature, "EDID", 4) == 0 && size > 0) {
					a_editorID.assign(reinterpret_cast<const char*>(a_data + position),
						strnlen(reinterpret_cast<const char*>(a_data + position), size));
					if (!a_isQuest) {
						return;
					}
				} else if (a_isQuest && std::memcmp(signature, "INDX", 4) == 0 && size >= 2) {
					std::uint16_t stage = 0;
					std::memcpy(&stage, a_data + position, sizeof(stage));
					a_stages.push_back(stage);
				}
				position += size;
			}
		}
	}

	bool ReadMasters(const std::string& a_path, std::vector<std::string>& a_output,
		bool& a_isLight, std::string& a_error)
	{
		a_output.clear();
		std::ifstream file(a_path, std::ios::binary);
		if (!file) {
			a_error = "cannot open " + a_path;
			return false;
		}
		RecordHeader header{};
		if (!ReadHeader(file, header) || !Signature(header.type, "TES4")) {
			a_error = "no TES4 header: " + a_path;
			return false;
		}
		a_isLight = (header.flags & kLightFlag) != 0;
		if (header.dataSize > kMaximumDecompressedSize) {
			a_error = "oversized TES4 header in " + a_path;
			return false;
		}
		std::vector<unsigned char> data(header.dataSize);
		if (!file.read(reinterpret_cast<char*>(data.data()), header.dataSize)) {
			a_error = "short TES4 header: " + a_path;
			return false;
		}
		std::size_t position = 0;
		while (position + 6 <= data.size()) {
			const char* signature = reinterpret_cast<const char*>(data.data() + position);
			std::uint16_t size = 0;
			std::memcpy(&size, data.data() + position + 4, sizeof(size));
			position += 6;
			if (position + size > data.size()) {
				break;
			}
			if (std::memcmp(signature, "MAST", 4) == 0 && size > 0) {
				a_output.emplace_back(reinterpret_cast<const char*>(data.data() + position),
					strnlen(reinterpret_cast<const char*>(data.data() + position), size));
			}
			position += size;
		}
		return true;
	}

	bool ScanFile(const std::string& a_path, const std::vector<std::uint32_t>& a_prefixes,
		const std::set<std::string>& a_wantedTypes, ScanResult& a_output, std::string& a_error)
	{
		if (a_prefixes.empty()) {
			a_error = "no runtime FormID prefixes";
			return false;
		}
		std::ifstream file(a_path, std::ios::binary);
		if (!file) {
			a_error = "cannot open " + a_path;
			return false;
		}

		const auto rebase = [&](std::uint32_t a_formID) {
			std::uint32_t masterIndex = a_formID >> 24;
			if (masterIndex >= a_prefixes.size() - 1) {
				masterIndex = static_cast<std::uint32_t>(a_prefixes.size() - 1);
			}
			const std::uint32_t prefix = a_prefixes[masterIndex];
			if (prefix == kBadPrefix) {
				return kBadPrefix;
			}
			if ((prefix & 0xFF000000) == 0xFE000000) {
				return prefix | (a_formID & 0xFFF);
			}
			return prefix | (a_formID & 0x00FFFFFF);
		};

		RecordHeader header{};
		std::vector<unsigned char> compressed;
		std::vector<unsigned char> decompressed;
		while (ReadHeader(file, header)) {
			if (Signature(header.type, "GRUP")) {
				if (header.dataSize < sizeof(RecordHeader)) {
					a_error = "corrupt GRUP in " + a_path;
					return false;
				}
				const auto groupType = static_cast<std::int32_t>(header.formID);
				char label[5]{};
				std::memcpy(label, &header.flags, 4);
				const bool pruneTop = groupType == 0 && !a_wantedTypes.contains(label) &&
					std::strcmp(label, "CELL") != 0 && std::strcmp(label, "WRLD") != 0;
				const bool pruneChildren = groupType >= 6 && groupType <= 10;
				if (pruneTop || pruneChildren) {
					file.seekg(static_cast<std::streamoff>(header.dataSize - sizeof(RecordHeader)), std::ios::cur);
				}
				continue;
			}

			const std::string type(header.type, 4);
			if (!a_wantedTypes.contains(type) || Signature(header.type, "TES4")) {
				file.seekg(header.dataSize, std::ios::cur);
				continue;
			}
			if (header.dataSize > kMaximumDecompressedSize) {
				a_error = "oversized record in " + a_path;
				return false;
			}
			compressed.resize(header.dataSize);
			if (!file.read(reinterpret_cast<char*>(compressed.data()), header.dataSize)) {
				break;
			}

			const unsigned char* data = compressed.data();
			std::size_t size = compressed.size();
			if ((header.flags & kCompressed) != 0) {
				if (size < 4) {
					continue;
				}
				std::uint32_t decompressedSize = 0;
				std::memcpy(&decompressedSize, data, sizeof(decompressedSize));
				if (decompressedSize == 0 || decompressedSize > kMaximumDecompressedSize) {
					continue;
				}
				decompressed.resize(decompressedSize);
				mz_ulong outputSize = decompressedSize;
				if (mz_uncompress(decompressed.data(), &outputSize, data + 4,
					static_cast<mz_ulong>(size - 4)) != MZ_OK) {
					continue;
				}
				data = decompressed.data();
				size = outputSize;
			}

			std::string editorID;
			std::vector<std::uint16_t> stages;
			WalkSubrecords(data, size, type == "QUST", editorID, stages);
			const std::uint32_t runtimeID = rebase(header.formID);
			if (runtimeID == kBadPrefix) {
				continue;
			}
			if (!editorID.empty()) {
				a_output.records.push_back({ runtimeID, std::move(editorID), type });
			}
			if (type == "QUST" && !stages.empty()) {
				std::ranges::sort(stages);
				stages.erase(std::ranges::unique(stages).begin(), stages.end());
				a_output.questStages[runtimeID] = std::move(stages);
			}
		}
		return true;
	}
}
