#include "Core.h"

#include <algorithm>

namespace eca
{
	namespace
	{
		std::string Trim(std::string_view a_text)
		{
			std::size_t begin = 0;
			std::size_t end = a_text.size();
			while (begin < end && (a_text[begin] == ' ' || a_text[begin] == '\t' || a_text[begin] == '\r')) {
				++begin;
			}
			while (end > begin && (a_text[end - 1] == ' ' || a_text[end - 1] == '\t' || a_text[end - 1] == '\r')) {
				--end;
			}
			return std::string(a_text.substr(begin, end - begin));
		}

		Domain DomainForParamType(int a_type)
		{
			switch (a_type) {
			case 0x3:
			case 0x35:
				return Domain::Item;
			case 0x4:
			case 0x3E:
				return Domain::Form;
			case 0x6:
			case 0x19:
			case 0x22:
				return Domain::Placeable;
			case 0x5:
				return Domain::ActorValue;
			case 0x7:
			case 0xB:
				return Domain::Spell;
			case 0x9:
				return Domain::Cell;
			case 0xE:
				return Domain::Quest;
			case 0xF:
				return Domain::Race;
			case 0x11:
				return Domain::Faction;
			case 0x13:
				return Domain::Global;
			case 0x14:
			case 0x25:
				return Domain::FormList;
			case 0x15:
			case 0x38:
				return Domain::Placeable;
			case 0x1B:
				return Domain::Worldspace;
			case 0x1F:
				return Domain::MagicEffect;
			case 0x21:
				return Domain::Weather;
			case 0x27:
				return Domain::Perk;
			case 0x2A:
				return Domain::Imod;
			case 0x3B:
				return Domain::Keyword;
			case 0x3D:
				return Domain::Location;
			case 0x40:
				return Domain::Shout;
			case 0x41:
				return Domain::WordOfPower;
			default:
				return Domain::None;
			}
		}

		Domain OverrideDomain(const CommandMeta& a_command)
		{
			const auto longName = Lower(a_command.longName);
			const auto shortName = Lower(a_command.shortName);
			if (longName == "setgamesetting" || longName == "getgamesetting" || shortName == "setgs" || shortName == "getgs") {
				return Domain::GameSetting;
			}
			if (longName == "centeroncell" || shortName == "coc") {
				return Domain::Cell;
			}
			if (longName == "centeronworld" || shortName == "cow") {
				return Domain::Worldspace;
			}
			if (longName == "setoutfit") {
				return Domain::Outfit;
			}
			return Domain::None;
		}

		std::string Signature(const CommandMeta& a_command)
		{
			std::string result = a_command.longName;
			for (const auto& param : a_command.params) {
				result += ' ';
				result += param.optional ? '[' : '<';
				result += param.name.empty() ? "arg" : param.name;
				result += param.optional ? ']' : '>';
			}
			return result;
		}

		void FindMatches(std::vector<Candidate>& a_matches, int& a_index,
			const std::vector<Candidate>& a_candidates, std::string_view a_fragment)
		{
			a_matches.clear();
			a_index = -1;
			const auto fragment = Lower(a_fragment);
			std::vector<Candidate> prefix;
			std::vector<Candidate> partial;
			for (const auto& candidate : a_candidates) {
				const auto value = Lower(candidate.display);
				const auto position = value.find(fragment);
				if (fragment.empty() || position == 0) {
					prefix.push_back(candidate);
				} else if (position != std::string::npos) {
					partial.push_back(candidate);
				}
			}
			a_matches = std::move(prefix);
			a_matches.insert(a_matches.end(), partial.begin(), partial.end());
			if (!a_matches.empty()) {
				a_index = 0;
			}
		}

		void FindCommandMatches(std::vector<Candidate>& a_matches, int& a_index,
			std::span<const CommandMeta> a_commands, std::string_view a_fragment, bool a_preferReference)
		{
			a_matches.clear();
			a_index = -1;
			const auto fragment = Lower(a_fragment);
			std::vector<Candidate> prefixRef;
			std::vector<Candidate> prefixOther;
			std::vector<Candidate> partialRef;
			std::vector<Candidate> partialOther;
			for (const auto& command : a_commands) {
				if (command.longName.empty()) {
					continue;
				}
				const auto longName = Lower(command.longName);
				const auto shortName = Lower(command.shortName);
				const auto longPos = longName.find(fragment);
				const auto shortPos = shortName.empty() ? std::string::npos : shortName.find(fragment);
				const bool prefix = fragment.empty() || longPos == 0 || shortPos == 0;
				const bool partial = longPos != std::string::npos || shortPos != std::string::npos;
				if (prefix) {
					(command.refRequired ? prefixRef : prefixOther).emplace_back(command.longName);
				} else if (partial) {
					(command.refRequired ? partialRef : partialOther).emplace_back(command.longName);
				}
			}
			auto append = [&](std::vector<Candidate>& a_values) {
				a_matches.insert(a_matches.end(), a_values.begin(), a_values.end());
			};
			if (a_preferReference) {
				append(prefixRef);
				append(prefixOther);
				append(partialRef);
				append(partialOther);
			} else {
				append(prefixOther);
				append(prefixRef);
				append(partialOther);
				append(partialRef);
			}
			if (!a_matches.empty()) {
				a_index = 0;
			}
		}

		void Promote(std::vector<Candidate>& a_matches, int& a_index, std::string_view a_name)
		{
			const auto name = Lower(a_name);
			for (std::size_t i = 0; i < a_matches.size(); ++i) {
				if (Lower(a_matches[i].display) == name) {
					std::rotate(a_matches.begin(), a_matches.begin() + static_cast<std::ptrdiff_t>(i),
						a_matches.begin() + static_cast<std::ptrdiff_t>(i + 1));
					a_index = 0;
					return;
				}
			}
			a_matches.insert(a_matches.begin(), Candidate(std::string(a_name)));
			a_index = 0;
		}

		void RenderMatchList(ViewState& a_view, const std::vector<Candidate>& a_matches,
			int a_index, int a_limit)
		{
			const auto count = static_cast<int>(a_matches.size());
			if (count <= 1 || a_limit <= 0 || a_index < 0) {
				return;
			}
			const int visible = std::min(count, a_limit);
			int start = std::clamp(a_index - visible / 2, 0, count - visible);
			for (int i = start; i < start + visible; ++i) {
				a_view.listLines.push_back((i == a_index ? "> " : "  ") + a_matches[static_cast<std::size_t>(i)].display);
			}
			a_view.counter = "[" + std::to_string(a_index + 1) + "/" + std::to_string(count) + "]";
		}
	}

	std::string Lower(std::string_view a_text)
	{
		std::string result(a_text);
		for (auto& character : result) {
			if (character >= 'A' && character <= 'Z') {
				character = static_cast<char>(character + ('a' - 'A'));
			}
		}
		return result;
	}

	const char* DomainLabel(Domain a_domain)
	{
		switch (a_domain) {
		case Domain::Command: return "commands";
		case Domain::Cell: return "cells";
		case Domain::Worldspace: return "worldspaces";
		case Domain::Item: return "items";
		case Domain::Placeable: return "placeable forms";
		case Domain::Quest: return "quests";
		case Domain::QuestStage: return "quest stages";
		case Domain::GameSetting: return "game settings";
		case Domain::ActorValue: return "actor values";
		case Domain::Weather: return "weathers";
		case Domain::Imod: return "imagespace modifiers";
		case Domain::Perk: return "perks";
		case Domain::Spell: return "spells";
		case Domain::Faction: return "factions";
		case Domain::Location: return "locations";
		case Domain::Keyword: return "keywords";
		case Domain::Global: return "globals";
		case Domain::Outfit: return "outfits";
		case Domain::FormList: return "form lists";
		case Domain::Race: return "races";
		case Domain::MagicEffect: return "magic effects";
		case Domain::Shout: return "shouts";
		case Domain::WordOfPower: return "words of power";
		case Domain::Enchantment: return "enchantments";
		case Domain::Form: return "forms";
		default: return "";
		}
	}

	ParseResult Parse(std::string_view a_line, const std::function<const CommandMeta*(std::string_view)>& a_find)
	{
		ParseResult result;
		std::size_t leading = 0;
		while (leading < a_line.size() && a_line[leading] == ' ') {
			++leading;
		}
		if (leading == a_line.size()) {
			return result;
		}

		const auto text = a_line.substr(leading);
		std::string_view body = text;
		std::string prefix;
		const auto dot = text.find('.');
		const auto firstSpace = text.find(' ');
		if (dot != std::string_view::npos && (firstSpace == std::string_view::npos || dot < firstSpace)) {
			prefix = std::string(text.substr(0, dot + 1));
			body = text.substr(dot + 1);
			result.hasRefPrefix = true;
			result.playerPrefix = Lower(text.substr(0, dot)) == "player";
		}

		const auto space = body.find(' ');
		if (space == std::string_view::npos) {
			result.domain = Domain::Command;
			result.head = std::string(a_line.substr(0, leading)) + prefix;
			result.frag = std::string(body);
			return result;
		}

		const auto* command = a_find(body.substr(0, space));
		if (!command) {
			return result;
		}
		std::size_t argumentStart = space;
		while (argumentStart < body.size() && body[argumentStart] == ' ') {
			++argumentStart;
		}
		const auto arguments = body.substr(argumentStart);
		const auto headBase = std::string(a_line.substr(0, leading)) + prefix + std::string(body.substr(0, argumentStart));

		if (const auto overrideDomain = OverrideDomain(*command); overrideDomain != Domain::None) {
			result.domain = overrideDomain;
			result.head = headBase;
			result.frag = std::string(arguments);
			return result;
		}
		if (command->params.empty()) {
			return result;
		}

		const auto argumentSpace = arguments.find(' ');
		if (command->params.size() >= 2 && command->params[0].type == 0xE &&
			(command->params[1].type == 0x17 || command->params[1].type == 0x1) &&
			argumentSpace != std::string_view::npos) {
			result.questID = std::string(arguments.substr(0, argumentSpace));
			std::size_t stageStart = argumentSpace;
			while (stageStart < arguments.size() && arguments[stageStart] == ' ') {
				++stageStart;
			}
			result.domain = Domain::QuestStage;
			result.head = headBase + std::string(arguments.substr(0, stageStart));
			result.frag = std::string(arguments.substr(stageStart));
			return result;
		}

		if (argumentSpace != std::string_view::npos) {
			return result;
		}
		result.domain = DomainForParamType(command->params[0].type);
		if (result.domain != Domain::None) {
			result.head = headBase;
			result.frag = std::string(arguments);
		}
		return result;
	}

	void HistoryStore::SetCap(std::size_t a_cap)
	{
		_cap = std::clamp<std::size_t>(a_cap, 1, 10000);
		if (_lines.size() > _cap) {
			_lines.erase(_lines.begin(), _lines.end() - static_cast<std::ptrdiff_t>(_cap));
		}
	}

	void HistoryStore::Load(std::vector<std::string> a_lines)
	{
		_lines = std::move(a_lines);
		SetCap(_cap);
	}

	bool HistoryStore::Push(std::string a_line)
	{
		a_line = Trim(a_line);
		if (a_line.empty() || (!_lines.empty() && _lines.back() == a_line)) {
			return false;
		}
		_lines.push_back(std::move(a_line));
		if (_lines.size() > _cap) {
			_lines.erase(_lines.begin());
		}
		return true;
	}

	const std::vector<std::string>& HistoryStore::Lines() const
	{
		return _lines;
	}

	std::size_t HistoryStore::Cap() const
	{
		return _cap;
	}

	ViewModel::ViewModel(ICandidates& a_candidates, const Config& a_config, const HistoryStore& a_history) :
		_candidates(a_candidates), _config(a_config), _history(a_history)
	{}

	void ViewModel::FilterHistory(std::string_view a_filter)
	{
		_historyMatches.clear();
		_historyIndex = -1;
		const auto filter = Lower(a_filter);
		for (auto iterator = _history.Lines().rbegin(); iterator != _history.Lines().rend(); ++iterator) {
			if (Lower(*iterator).find(filter) != std::string::npos) {
				_historyMatches.push_back(*iterator);
			}
		}
		if (!_historyMatches.empty()) {
			_historyIndex = 0;
		}
	}

	ViewState ViewModel::RenderHistory() const
	{
		ViewState view;
		const bool valid = _historyIndex >= 0 && _historyIndex < static_cast<int>(_historyMatches.size());
		view.ghost = std::string("(history) ") + (valid ? _historyMatches[static_cast<std::size_t>(_historyIndex)] : "no match");
		const int count = static_cast<int>(_historyMatches.size());
		if (count > 0 && _config.matchListSize > 0) {
			const int visible = std::min(count, _config.matchListSize);
			const int start = std::clamp(_historyIndex - visible / 2, 0, count - visible);
			for (int i = start; i < start + visible; ++i) {
				view.listLines.push_back((i == _historyIndex ? "> " : "  ") + _historyMatches[static_cast<std::size_t>(i)]);
			}
			view.counter = "[" + std::to_string(_historyIndex + 1) + "/" + std::to_string(count) + "]";
		}
		return view;
	}

	ViewState ViewModel::OnInput(std::string_view a_text, std::string_view a_event)
	{
		ViewState view;
		if (a_event == "reset") {
			_matches.clear();
			_matchIndex = -1;
			_lastDomain = Domain::None;
			_lastInput.clear();
			_lastAccepted.clear();
			_historyActive = false;
			_historyMatches.clear();
			_historyIndex = -1;
			return view;
		}

		if (a_event == "ctrlr") {
			if (_historyActive) {
				if (_historyIndex + 1 < static_cast<int>(_historyMatches.size())) {
					++_historyIndex;
				}
			} else {
				_historyActive = true;
				FilterHistory(a_text);
			}
			return RenderHistory();
		}
		if (a_event == "shiftctrlr") {
			if (_historyActive && _historyIndex > 0) {
				--_historyIndex;
			}
			return _historyActive ? RenderHistory() : view;
		}

		if (_historyActive) {
			if (a_event == "change") {
				FilterHistory(a_text);
				return RenderHistory();
			}
			if (a_event == "tab") {
				if (_historyIndex >= 0 && _historyIndex < static_cast<int>(_historyMatches.size())) {
					view.setEntry = true;
					view.entryText = _historyMatches[static_cast<std::size_t>(_historyIndex)];
					view.caretPos = static_cast<int>(view.entryText.size());
				}
				_historyActive = false;
				_historyMatches.clear();
				_historyIndex = -1;
				_matches.clear();
				return view;
			}
			return RenderHistory();
		}

		const auto find = [&](std::string_view a_name) { return _candidates.FindCommand(a_name); };
		if (a_event == "change") {
			if (std::string(a_text) != _lastAccepted) {
				_matches.clear();
				_matchIndex = -1;
				_lastAccepted.clear();
			}
			const auto parse = Parse(a_text, find);
			if ((parse.domain == Domain::GameSetting || parse.domain == Domain::ActorValue) && !parse.frag.empty() &&
				(parse.domain != Domain::ActorValue || parse.playerPrefix || _candidates.HasPickRef())) {
				view.ghost = _candidates.LiveValue(parse.domain, parse.frag);
				if (!view.ghost.empty()) {
					return view;
				}
			}
			if (parse.domain == Domain::Command && !parse.frag.empty()) {
				std::vector<Candidate> matches;
				int index = -1;
				FindCommandMatches(matches, index, _candidates.Commands(), parse.frag, false);
				if (index >= 0) {
					if (const auto* command = _candidates.FindCommand(matches[static_cast<std::size_t>(index)].display)) {
						view.ghost = parse.head + Signature(*command);
					}
				}
			} else if (parse.domain != Domain::None) {
				if (_candidates.List(parse.domain, parse) == nullptr) {
					view.ghost = std::string("(loading ") + DomainLabel(parse.domain) + "...)";
				} else if (parse.frag.empty()) {
					view.ghost = std::string("TAB: ") + DomainLabel(parse.domain);
				}
			}
			return view;
		}

		if (a_event != "tab" && a_event != "shifttab") {
			return view;
		}
		const bool cycling = std::string(a_text) == _lastAccepted && _matches.size() > 1;
		ParseResult parse;
		if (cycling) {
			if (a_event == "tab") {
				_matchIndex = (_matchIndex + 1) % static_cast<int>(_matches.size());
			} else {
				_matchIndex = (_matchIndex - 1 + static_cast<int>(_matches.size())) % static_cast<int>(_matches.size());
			}
			parse = Parse(_lastInput, find);
		} else {
			parse = Parse(a_text, find);
			if (parse.domain == Domain::None) {
				return view;
			}
			if (parse.domain == Domain::Command) {
				FindCommandMatches(_matches, _matchIndex, _candidates.Commands(), parse.frag,
					parse.hasRefPrefix || _candidates.HasPickRef());
				if (!parse.hasRefPrefix && !parse.frag.empty() && std::string_view("player").starts_with(Lower(parse.frag))) {
					Promote(_matches, _matchIndex, "player");
				}
			} else {
				const auto* candidates = _candidates.List(parse.domain, parse);
				if (!candidates) {
					view.ghost = std::string("(loading ") + DomainLabel(parse.domain) + "...)";
					return view;
				}
				FindMatches(_matches, _matchIndex, *candidates, parse.frag);
			}
			_lastDomain = parse.domain;
			_lastInput = std::string(a_text);
		}

		if (_matchIndex < 0 || _matchIndex >= static_cast<int>(_matches.size())) {
			return view;
		}
		const auto& current = _matches[static_cast<std::size_t>(_matchIndex)];
		if (_lastDomain == Domain::Command && Lower(current.display) == "player" && !parse.hasRefPrefix) {
			view.entryText = parse.head + "player.";
		} else {
			view.entryText = parse.head + current.insert;
		}
		view.setEntry = true;
		view.caretPos = static_cast<int>(view.entryText.size());
		_lastAccepted = view.entryText;
		if (_lastDomain == Domain::Command) {
			if (const auto* command = _candidates.FindCommand(current.display)) {
				view.ghost = parse.head + Signature(*command);
			}
		}
		RenderMatchList(view, _matches, _matchIndex, _config.matchListSize);
		return view;
	}
}
