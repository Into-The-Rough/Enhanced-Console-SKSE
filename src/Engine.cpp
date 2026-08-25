#include "Engine.h"

#include "EspScan.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cctype>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <thread>
#include <unordered_map>

namespace eca::engine
{
	namespace
	{
		using RawCandidate = std::pair<std::string, std::uint32_t>;

		Config g_config;
		HistoryStore g_history;
		std::size_t g_historyFileLines{ 0 };
		using EditorIDMap = RE::BSTHashMap<RE::BSFixedString, RE::TESForm*>;
		static_assert(sizeof(EditorIDMap) == 0x30);
		constexpr REL::ID kInsertEditorID{ 13808 };

		bool InsertEditorID(EditorIDMap* a_map, const RE::BSFixedString& a_editorID, RE::TESForm* a_form)
		{
			using func_t = bool (*)(void*, const RE::BSFixedString*, RE::TESForm* const*);
			static REL::Relocation<func_t> func{ kInsertEditorID };
			//the native insert function takes the scatter-table body after its 8-byte header.
			auto* table = reinterpret_cast<std::byte*>(a_map) + 0x8;
			return func(table, std::addressof(a_editorID), std::addressof(a_form));
		}
		int g_recallOffset{ 0 };
		std::vector<CommandMeta> g_commands;
		std::map<Domain, std::vector<RawCandidate>> g_harvested;
		std::map<Domain, std::vector<Candidate>> g_candidates;
		std::unordered_map<std::string, std::vector<std::uint16_t>> g_questStages;
		std::vector<std::pair<std::uint32_t, std::string>> g_injectable;
		std::atomic<bool> g_harvestReady{ false };
		bool g_injectionDone{ false };
		const std::vector<Candidate> g_disabled;

		const std::set<std::string> g_wantedTypes{
			"WEAP", "AMMO", "ARMO", "ALCH", "INGR", "MISC", "BOOK", "KEYM", "SLGM", "SCRL", "LIGH",
			"NPC_", "FURN", "STAT", "MSTT", "ACTI", "CONT", "DOOR", "TREE", "FLOR",
			"WTHR", "PERK", "SPEL", "QUST", "CELL", "GMST", "WRLD", "IMAD", "FACT", "LCTN",
			"KYWD", "GLOB", "OTFT", "FLST", "RACE", "MGEF", "SHOU", "WOOP", "ENCH"
		};
		const std::set<std::string> g_itemTypes{
			"WEAP", "AMMO", "ARMO", "ALCH", "INGR", "MISC", "BOOK", "KEYM", "SLGM", "SCRL", "LIGH"
		};
		const std::set<std::string> g_placeableTypes{
			"NPC_", "FURN", "STAT", "MSTT", "ACTI", "CONT", "DOOR", "TREE", "FLOR"
		};

		std::filesystem::path HistoryPath()
		{
			const auto directory = SKSE::log::log_directory();
			return directory ? *directory / "EnhancedConsoleSSE_history.txt" : std::filesystem::path{};
		}

		std::vector<std::string> LoadHistory()
		{
			std::vector<std::string> lines;
			const auto path = HistoryPath();
			if (path.empty()) {
				return lines;
			}
			std::ifstream file(path, std::ios::binary);
			std::string line;
			while (std::getline(file, line)) {
				if (!line.empty() && line.back() == '\r') {
					line.pop_back();
				}
				if (!line.empty()) {
					lines.push_back(line);
				}
			}
			return lines;
		}

		void RewriteHistory()
		{
			const auto path = HistoryPath();
			if (path.empty()) {
				return;
			}
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			for (const auto& line : g_history.Lines()) {
				file << line << '\n';
			}
			g_historyFileLines = g_history.Lines().size();
		}

		void LoadConfig()
		{
			const auto modulePath = REL::Module::get().filePath();
			auto path = std::filesystem::path(std::wstring(modulePath));
			path = path.parent_path() / "Data" / "SKSE" / "Plugins" / "EnhancedConsoleSSE.ini";

			std::unordered_map<std::string, int> values;
			std::ifstream file(path);
			bool inGeneral = false;
			std::string line;
			while (std::getline(file, line)) {
				const auto trim = [](std::string_view a_value) {
					while (!a_value.empty() && std::isspace(static_cast<unsigned char>(a_value.front()))) {
						a_value.remove_prefix(1);
					}
					while (!a_value.empty() && std::isspace(static_cast<unsigned char>(a_value.back()))) {
						a_value.remove_suffix(1);
					}
					return a_value;
				};

				const auto value = trim(line);
				if (value.empty() || value.front() == ';' || value.front() == '#') {
					continue;
				}
				if (value.front() == '[' && value.back() == ']') {
					inGeneral = trim(value.substr(1, value.size() - 2)) == "General";
					continue;
				}
				if (!inGeneral) {
					continue;
				}
				const auto equals = value.find('=');
				if (equals == std::string_view::npos) {
					continue;
				}
				const auto key = trim(value.substr(0, equals));
				const auto number = trim(value.substr(equals + 1));
				int parsed{};
				const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), parsed);
				if (error == std::errc{} && end == number.data() + number.size()) {
					values.emplace(key, parsed);
				}
			}
			const auto read = [&](const char* a_key, int a_default) {
				const auto found = values.find(a_key);
				return found != values.end() ? found->second : a_default;
			};
			g_config.matchListSize = std::clamp(read("iMatchListSize", 7), 0, 20);
			g_config.useTab = read("bUseTab", 1) != 0;
			g_config.injectEditorIDs = read("bInjectEditorIDs", 1) != 0;
			g_config.completeItems = read("bCompleteItems", 1) != 0;
			g_config.completeCells = read("bCompleteCells", 1) != 0;
			g_config.completeQuests = read("bCompleteQuests", 1) != 0;
			g_config.completeSettings = read("bCompleteSettings", 1) != 0;
			g_config.completeActorValues = read("bCompleteActorValues", 1) != 0;
			g_config.completeWeather = read("bCompleteWeather", 1) != 0;
			g_config.completePerksSpells = read("bCompletePerksSpells", 1) != 0;
			g_config.completeWorldspaces = read("bCompleteWorldspaces", 1) != 0;
			g_config.completeFactions = read("bCompleteFactions", 1) != 0;
			g_config.completeLocations = read("bCompleteLocations", 1) != 0;
			g_config.completeKeywordsGlobals = read("bCompleteKeywordsGlobals", 1) != 0;
			g_config.completeSkyrimForms = read("bCompleteSkyrimForms", 1) != 0;
			g_config.persistHistory = read("bPersistHistory", 1) != 0;
			g_config.historySize = std::clamp(read("iHistorySize", 1000), 1, 10000);
		}

		void BuildCommands()
		{
			const auto append = [](RE::SCRIPT_FUNCTION* a_first, std::uint32_t a_count) {
				if (!a_first) {
					return;
				}
				for (std::uint32_t index = 0; index < a_count; ++index) {
					const auto& function = a_first[index];
					if (!function.functionName || function.functionName[0] == '\0') {
						continue;
					}
					CommandMeta command;
					command.longName = function.functionName;
					command.shortName = function.shortName ? function.shortName : "";
					command.help = function.helpString ? function.helpString : "";
					command.refRequired = function.referenceFunction;
					for (std::uint16_t parameterIndex = 0;
						parameterIndex < function.numParams && function.params; ++parameterIndex) {
						const auto& parameter = function.params[parameterIndex];
						command.params.push_back({
							parameter.paramName ? parameter.paramName : "",
							static_cast<int>(parameter.paramType.underlying()),
							parameter.optional
						});
					}
					g_commands.push_back(std::move(command));
				}
			};
			append(RE::SCRIPT_FUNCTION::GetFirstConsoleCommand(),
				RE::SCRIPT_FUNCTION::Commands::kConsoleCommandsEnd);
			append(RE::SCRIPT_FUNCTION::GetFirstScriptCommand(),
				RE::SCRIPT_FUNCTION::Commands::kScriptCommandsEnd);
		}

		void AddRecord(const esp::Record& a_record)
		{
			const RawCandidate value{ a_record.editorID, a_record.formID };
			if (g_itemTypes.contains(a_record.type)) {
				g_harvested[Domain::Item].push_back(value);
				g_harvested[Domain::Placeable].push_back(value);
			} else if (g_placeableTypes.contains(a_record.type)) {
				g_harvested[Domain::Placeable].push_back(value);
			} else if (a_record.type == "CELL") {
				g_harvested[Domain::Cell].push_back(value);
			} else if (a_record.type == "WRLD") {
				g_harvested[Domain::Worldspace].push_back(value);
			} else if (a_record.type == "QUST") {
				g_harvested[Domain::Quest].push_back(value);
			} else if (a_record.type == "GMST") {
				g_harvested[Domain::GameSetting].push_back(value);
			} else if (a_record.type == "WTHR") {
				g_harvested[Domain::Weather].push_back(value);
			} else if (a_record.type == "IMAD") {
				g_harvested[Domain::Imod].push_back(value);
			} else if (a_record.type == "PERK") {
				g_harvested[Domain::Perk].push_back(value);
			} else if (a_record.type == "SPEL") {
				g_harvested[Domain::Spell].push_back(value);
			} else if (a_record.type == "FACT") {
				g_harvested[Domain::Faction].push_back(value);
			} else if (a_record.type == "LCTN") {
				g_harvested[Domain::Location].push_back(value);
			} else if (a_record.type == "KYWD") {
				g_harvested[Domain::Keyword].push_back(value);
			} else if (a_record.type == "GLOB") {
				g_harvested[Domain::Global].push_back(value);
			} else if (a_record.type == "OTFT") {
				g_harvested[Domain::Outfit].push_back(value);
			} else if (a_record.type == "FLST") {
				g_harvested[Domain::FormList].push_back(value);
			} else if (a_record.type == "RACE") {
				g_harvested[Domain::Race].push_back(value);
			} else if (a_record.type == "MGEF") {
				g_harvested[Domain::MagicEffect].push_back(value);
			} else if (a_record.type == "SHOU") {
				g_harvested[Domain::Shout].push_back(value);
			} else if (a_record.type == "WOOP") {
				g_harvested[Domain::WordOfPower].push_back(value);
			} else if (a_record.type == "ENCH") {
				g_harvested[Domain::Enchantment].push_back(value);
			}
			if (a_record.type != "GMST" && a_record.type != "CELL") {
				g_harvested[Domain::Form].push_back(value);
				g_injectable.emplace_back(a_record.formID, a_record.editorID);
			}
		}

		void Dedupe(std::vector<RawCandidate>& a_values)
		{
			std::set<std::string> seen;
			std::vector<RawCandidate> output;
			output.reserve(a_values.size());
			for (auto& value : a_values) {
				if (seen.insert(Lower(value.first)).second) {
					output.push_back(std::move(value));
				}
			}
			a_values = std::move(output);
		}

		void Harvest()
		{
			try {
				const auto* dataHandler = RE::TESDataHandler::GetSingleton();
				if (!dataHandler) {
					g_harvestReady.store(true, std::memory_order_release);
					return;
				}
				std::unordered_map<std::string, std::uint32_t> prefixesByName;
				std::vector<const RE::TESFile*> files;
				for (const auto* file : dataHandler->compiledFileCollection.files) {
					if (!file) {
						continue;
					}
					prefixesByName[Lower(file->GetFilename())] = static_cast<std::uint32_t>(file->GetCompileIndex()) << 24;
					files.push_back(file);
				}
				for (const auto* file : dataHandler->compiledFileCollection.smallFiles) {
					if (!file) {
						continue;
					}
					prefixesByName[Lower(file->GetFilename())] =
						0xFE000000u | (static_cast<std::uint32_t>(file->GetSmallFileCompileIndex()) << 12);
					files.push_back(file);
				}

				std::unordered_map<std::uint32_t, std::vector<std::uint16_t>> stagesByID;
				std::unordered_map<std::uint32_t, std::string> questEditorIDs;
				for (const auto* file : files) {
					const std::string filename(file->GetFilename());
					const std::string path = "Data\\" + filename;
					std::vector<std::string> masters;
					bool light = false;
					std::string error;
					if (!esp::ReadMasters(path, masters, light, error)) {
						SKSE::log::warn("plugin scan: {}", error);
						continue;
					}
					std::vector<std::uint32_t> prefixes;
					for (const auto& master : masters) {
						const auto iterator = prefixesByName.find(Lower(master));
						prefixes.push_back(iterator == prefixesByName.end() ? esp::kBadPrefix : iterator->second);
					}
					prefixes.push_back(prefixesByName.at(Lower(filename)));
					esp::ScanResult result;
					if (!esp::ScanFile(path, prefixes, g_wantedTypes, result, error)) {
						SKSE::log::warn("plugin scan: {}", error);
						continue;
					}
					for (const auto& record : result.records) {
						AddRecord(record);
						if (record.type == "QUST") {
							questEditorIDs[record.formID] = record.editorID;
						}
					}
					for (auto& [formID, stages] : result.questStages) {
						auto& output = stagesByID[formID];
						output.insert(output.end(), stages.begin(), stages.end());
					}
				}
				for (auto& [formID, stages] : stagesByID) {
					const auto editorID = questEditorIDs.find(formID);
					if (editorID == questEditorIDs.end()) {
						continue;
					}
					std::ranges::sort(stages);
					stages.erase(std::ranges::unique(stages).begin(), stages.end());
					g_questStages[Lower(editorID->second)] = std::move(stages);
				}
				for (auto& [domain, values] : g_harvested) {
					Dedupe(values);
				}
				SKSE::log::info("plugin scan complete: {} forms, {} items, {} cells, {} quests",
					g_harvested[Domain::Form].size(), g_harvested[Domain::Item].size(),
					g_harvested[Domain::Cell].size(), g_harvested[Domain::Quest].size());
			} catch (const std::exception& error) {
				SKSE::log::warn("plugin scan failed: {}", error.what());
			} catch (...) {
				SKSE::log::warn("plugin scan failed with an unknown exception");
			}
			g_harvestReady.store(true, std::memory_order_release);
		}

		bool DomainEnabled(Domain a_domain)
		{
			switch (a_domain) {
			case Domain::Item:
			case Domain::Placeable:
				return g_config.completeItems;
			case Domain::Cell:
				return g_config.completeCells;
			case Domain::Quest:
			case Domain::QuestStage:
				return g_config.completeQuests;
			case Domain::GameSetting:
				return g_config.completeSettings;
			case Domain::ActorValue:
				return g_config.completeActorValues;
			case Domain::Weather:
				return g_config.completeWeather;
			case Domain::Perk:
			case Domain::Spell:
				return g_config.completePerksSpells;
			case Domain::Worldspace:
				return g_config.completeWorldspaces;
			case Domain::Faction:
				return g_config.completeFactions;
			case Domain::Location:
				return g_config.completeLocations;
			case Domain::Keyword:
			case Domain::Global:
				return g_config.completeKeywordsGlobals;
			default:
				return g_config.completeSkyrimForms;
			}
		}

		class GameCandidates final : public ICandidates
		{
		public:
			std::span<const CommandMeta> Commands() override
			{
				return g_commands;
			}

			const CommandMeta* FindCommand(std::string_view a_name) override
			{
				const auto name = Lower(a_name);
				for (const auto& command : g_commands) {
					if (Lower(command.longName) == name || (!command.shortName.empty() && Lower(command.shortName) == name)) {
						return &command;
					}
				}
				return nullptr;
			}

			const std::vector<Candidate>* List(Domain a_domain, const ParseResult& a_parse) override
			{
				if (!DomainEnabled(a_domain)) {
					return &g_disabled;
				}
				if (a_domain == Domain::ActorValue) {
					auto& output = g_candidates[a_domain];
					if (output.empty()) {
						for (int index = 0; index < std::to_underlying(RE::ActorValue::kTotal); ++index) {
							const auto actorValue = static_cast<RE::ActorValue>(index);
							const auto* info = RE::ActorValueList::GetActorValueInfo(actorValue);
							const auto* enumName = info ? info->enumName : nullptr;
							if (!enumName || !enumName[0]) {
								continue;
							}
							const auto* displayName = RE::ActorValueList::GetActorValueName(actorValue);
							if (displayName && displayName[0] && Lower(displayName) != Lower(enumName)) {
								output.emplace_back(std::format("{} ({})", enumName, displayName), enumName);
							} else {
								output.emplace_back(enumName);
							}
						}
					}
					return &output;
				}
				if (!g_harvestReady.load(std::memory_order_acquire)) {
					return nullptr;
				}
				if (a_domain == Domain::QuestStage) {
					_stageCandidates.clear();
					if (const auto iterator = g_questStages.find(Lower(a_parse.questID)); iterator != g_questStages.end()) {
						for (const auto stage : iterator->second) {
							_stageCandidates.emplace_back(std::to_string(stage));
						}
					}
					return &_stageCandidates;
				}

				auto& output = g_candidates[a_domain];
				if (output.empty()) {
					const auto& raw = g_harvested[a_domain];
					output.reserve(raw.size());
					for (const auto& [editorID, formID] : raw) {
						if (a_domain == Domain::Cell || a_domain == Domain::GameSetting) {
							output.emplace_back(editorID);
							continue;
						}
						const auto* expected = RE::TESForm::LookupByID(formID);
						const auto* resolved = RE::TESForm::LookupByEditorID(editorID);
						output.emplace_back(editorID,
							resolved && resolved == expected ? editorID : std::format("{:08X}", formID));
					}
				}
				return &output;
			}

			bool HasPickRef() override
			{
				return static_cast<bool>(RE::Console::GetSelectedRef());
			}

			std::string LiveValue(Domain a_domain, std::string_view a_fragment) override
			{
				const std::string fragment(a_fragment);
				if (a_domain == Domain::GameSetting) {
					auto* collection = RE::GameSettingCollection::GetSingleton();
					auto* setting = collection ? collection->GetSetting(fragment.c_str()) : nullptr;
					if (!setting) {
						return {};
					}
					switch (setting->GetType()) {
					case RE::Setting::Type::kBool:
						return std::format("{} = {}", setting->GetName(), setting->GetBool() ? 1 : 0);
					case RE::Setting::Type::kFloat:
						return std::format("{} = {:.2f}", setting->GetName(), setting->GetFloat());
					case RE::Setting::Type::kCharacter:
						return std::format("{} = {}", setting->GetName(), setting->GetCharacter());
					case RE::Setting::Type::kUnsignedCharacter:
						return std::format("{} = {}", setting->GetName(), setting->GetUnsignedCharacter());
					case RE::Setting::Type::kInteger:
						return std::format("{} = {}", setting->GetName(), setting->GetInteger());
					case RE::Setting::Type::kUnsignedInteger:
						return std::format("{} = {}", setting->GetName(), setting->GetUnsignedInteger());
					case RE::Setting::Type::kString:
						return std::format("{} = {}", setting->GetName(), setting->GetString());
					default:
						return {};
					}
				}
				if (a_domain == Domain::ActorValue) {
					const auto actorValue = RE::ActorValueList::LookupActorValueByName(fragment.c_str());
					if (actorValue == RE::ActorValue::kNone) {
						return {};
					}
					RE::Actor* actor = nullptr;
					if (const auto selected = RE::Console::GetSelectedRef()) {
						actor = selected->As<RE::Actor>();
					}
					if (!actor) {
						actor = RE::PlayerCharacter::GetSingleton();
					}
					return actor ? std::format("{} = {:.2f}", fragment, actor->GetActorValue(actorValue)) : std::string{};
				}
				return {};
			}

		private:
			std::vector<Candidate> _stageCandidates;
		};

		GameCandidates g_gameCandidates;
	}

	Config& GetConfig()
	{
		return g_config;
	}

	ICandidates& GetCandidates()
	{
		return g_gameCandidates;
	}

	HistoryStore& GetHistory()
	{
		return g_history;
	}

	void OnDataLoaded()
	{
		LoadConfig();
		BuildCommands();
		g_history.SetCap(static_cast<std::size_t>(g_config.historySize));
		if (g_config.persistHistory) {
			auto lines = LoadHistory();
			g_historyFileLines = lines.size();
			g_history.Load(std::move(lines));
		}
		SKSE::log::info("loaded {} commands and {} saved history entries", g_commands.size(), g_history.Lines().size());
		std::thread(Harvest).detach();
	}

	bool InjectEditorIDsIfReady()
	{
		if (g_injectionDone || !g_harvestReady.load(std::memory_order_acquire)) {
			return g_injectionDone;
		}
		g_injectionDone = true;
		if (!g_config.injectEditorIDs) {
			return true;
		}
		try {
			struct Pending
			{
				RE::BSFixedString editorID;
				RE::TESForm* form;
			};
			std::vector<Pending> pending;
			for (const auto& [formID, editorID] : g_injectable) {
				auto* form = RE::TESForm::LookupByID(formID);
				if (form && !RE::TESForm::LookupByEditorID(editorID)) {
					pending.push_back({ RE::BSFixedString(editorID), form });
				}
			}
			const auto& [map, lock] = RE::TESForm::GetAllFormsByEditorID();
			std::size_t inserted = 0;
			if (map) {
				RE::BSWriteLockGuard guard(lock);
				for (const auto& entry : pending) {
					if (InsertEditorID(map, entry.editorID, entry.form)) {
						++inserted;
					}
				}
			}
			SKSE::log::info("injected {} editor IDs into the console lookup table", inserted);
		} catch (const std::exception& error) {
			SKSE::log::warn("editor ID injection failed: {}", error.what());
		} catch (...) {
			SKSE::log::warn("editor ID injection failed with an unknown exception");
		}
		return true;
	}

	void PushHistory(std::string a_command)
	{
		g_recallOffset = 0;
		if (!g_history.Push(std::move(a_command)) || !g_config.persistHistory) {
			return;
		}
		const auto path = HistoryPath();
		if (path.empty()) {
			return;
		}
		std::ofstream file(path, std::ios::binary | std::ios::app);
		if (file) {
			file << g_history.Lines().back() << '\n';
			++g_historyFileLines;
		}
		if (g_historyFileLines > 2 * g_history.Cap()) {
			RewriteHistory();
		}
	}

	bool RecallOlder(std::string& a_command)
	{
		const int count = static_cast<int>(g_history.Lines().size());
		if (g_recallOffset >= count) {
			return false;
		}
		++g_recallOffset;
		a_command = g_history.Lines()[static_cast<std::size_t>(count - g_recallOffset)];
		return true;
	}

	bool RecallNewer(std::string& a_command)
	{
		if (g_recallOffset <= 0) {
			return false;
		}
		--g_recallOffset;
		if (g_recallOffset == 0) {
			a_command.clear();
		} else {
			const int count = static_cast<int>(g_history.Lines().size());
			a_command = g_history.Lines()[static_cast<std::size_t>(count - g_recallOffset)];
		}
		return true;
	}

	void ResetRecall()
	{
		g_recallOffset = 0;
	}
}
