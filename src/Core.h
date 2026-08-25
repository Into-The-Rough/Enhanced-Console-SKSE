#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eca
{
	enum class Domain
	{
		None,
		Command,
		Cell,
		Worldspace,
		Item,
		Placeable,
		Quest,
		QuestStage,
		GameSetting,
		ActorValue,
		Weather,
		Imod,
		Perk,
		Spell,
		Faction,
		Location,
		Keyword,
		Global,
		Outfit,
		FormList,
		Race,
		MagicEffect,
		Shout,
		WordOfPower,
		Enchantment,
		Form
	};

	struct Candidate
	{
		std::string display;
		std::string insert;

		Candidate() = default;
		explicit Candidate(std::string a_display) :
			display(std::move(a_display)), insert(display)
		{}
		Candidate(std::string a_display, std::string a_insert) :
			display(std::move(a_display)), insert(std::move(a_insert))
		{}
	};

	struct CommandParam
	{
		std::string name;
		int type{ 0 };
		bool optional{ false };
	};

	struct CommandMeta
	{
		std::string longName;
		std::string shortName;
		std::string help;
		bool refRequired{ false };
		std::vector<CommandParam> params;
	};

	struct ParseResult
	{
		Domain domain{ Domain::None };
		std::string head;
		std::string frag;
		std::string questID;
		bool hasRefPrefix{ false };
		bool playerPrefix{ false };
	};

	struct ViewState
	{
		bool setEntry{ false };
		std::string entryText;
		int caretPos{ 0 };
		std::string ghost;
		std::vector<std::string> listLines;
		std::string counter;
	};

	struct Config
	{
		int matchListSize{ 7 };
		bool useTab{ true };
		bool injectEditorIDs{ true };
		bool completeItems{ true };
		bool completeCells{ true };
		bool completeQuests{ true };
		bool completeSettings{ true };
		bool completeActorValues{ true };
		bool completeWeather{ true };
		bool completePerksSpells{ true };
		bool completeWorldspaces{ true };
		bool completeFactions{ true };
		bool completeLocations{ true };
		bool completeKeywordsGlobals{ true };
		bool completeSkyrimForms{ true };
		bool persistHistory{ true };
		int historySize{ 1000 };
	};

	class ICandidates
	{
	public:
		virtual ~ICandidates() = default;
		virtual std::span<const CommandMeta> Commands() = 0;
		virtual const CommandMeta* FindCommand(std::string_view a_name) = 0;
		virtual const std::vector<Candidate>* List(Domain a_domain, const ParseResult& a_parse) = 0;
		virtual bool HasPickRef() = 0;
		virtual std::string LiveValue(Domain a_domain, std::string_view a_fragment) = 0;
	};

	class HistoryStore
	{
	public:
		void SetCap(std::size_t a_cap);
		void Load(std::vector<std::string> a_lines);
		bool Push(std::string a_line);
		[[nodiscard]] const std::vector<std::string>& Lines() const;
		[[nodiscard]] std::size_t Cap() const;

	private:
		std::vector<std::string> _lines;
		std::size_t _cap{ 1000 };
	};

	class ViewModel
	{
	public:
		ViewModel(ICandidates& a_candidates, const Config& a_config, const HistoryStore& a_history);
		ViewState OnInput(std::string_view a_text, std::string_view a_event);

	private:
		void FilterHistory(std::string_view a_filter);
		ViewState RenderHistory() const;

		ICandidates& _candidates;
		const Config& _config;
		const HistoryStore& _history;
		std::vector<Candidate> _matches;
		int _matchIndex{ -1 };
		Domain _lastDomain{ Domain::None };
		std::string _lastInput;
		std::string _lastAccepted;
		bool _historyActive{ false };
		std::vector<std::string> _historyMatches;
		int _historyIndex{ -1 };
	};

	std::string Lower(std::string_view a_text);
	ParseResult Parse(std::string_view a_line, const std::function<const CommandMeta*(std::string_view)>& a_find);
	const char* DomainLabel(Domain a_domain);
}
