#include "Core.h"

#ifdef NDEBUG
#	undef NDEBUG
#endif
#include <cassert>
#include <map>

namespace
{
	class Candidates final : public eca::ICandidates
	{
	public:
		std::span<const eca::CommandMeta> Commands() override
		{
			return commands;
		}

		const eca::CommandMeta* FindCommand(std::string_view a_name) override
		{
			const auto name = eca::Lower(a_name);
			for (const auto& command : commands) {
				if (eca::Lower(command.longName) == name || eca::Lower(command.shortName) == name) {
					return &command;
				}
			}
			return nullptr;
		}

		const std::vector<eca::Candidate>* List(eca::Domain a_domain, const eca::ParseResult&) override
		{
			return &lists[a_domain];
		}

		bool HasPickRef() override
		{
			return false;
		}

		std::string LiveValue(eca::Domain, std::string_view) override
		{
			return {};
		}

		std::vector<eca::CommandMeta> commands{
			{ "AddItem", "", "", true, { { "item", 0x3, false }, { "count", 0x1, true } } },
			{ "CenterOnCell", "coc", "", false, { { "cell", 0x0, false } } },
			{ "SetStage", "", "", false, { { "quest", 0xE, false }, { "stage", 0x17, false } } },
			{ "SetActorValue", "SetAV", "", true, { { "actor value", 0x5, false }, { "value", 0x2, false } } }
		};
		std::map<eca::Domain, std::vector<eca::Candidate>> lists{
			{ eca::Domain::Item, { eca::Candidate("Gold001"), eca::Candidate("GoldOre") } },
			{ eca::Domain::Cell, { eca::Candidate("WhiterunDragonsreach") } },
			{ eca::Domain::QuestStage, { eca::Candidate("10"), eca::Candidate("20") } },
			{ eca::Domain::ActorValue, { eca::Candidate("Marksman (Archery)", "Marksman") } }
		};
	};
}

int main()
{
	Candidates candidates;
	const auto find = [&](std::string_view a_name) { return candidates.FindCommand(a_name); };

	const auto command = eca::Parse("pla", find);
	assert(command.domain == eca::Domain::Command);
	assert(command.frag == "pla");

	const auto item = eca::Parse("player.additem gold", find);
	assert(item.domain == eca::Domain::Item);
	assert(item.playerPrefix);
	assert(item.frag == "gold");

	const auto cell = eca::Parse("coc white", find);
	assert(cell.domain == eca::Domain::Cell);
	assert(cell.frag == "white");

	const auto stage = eca::Parse("setstage MQ101 1", find);
	assert(stage.domain == eca::Domain::QuestStage);
	assert(stage.questID == "MQ101");

	eca::HistoryStore history;
	history.Push("help gold");
	history.Push("coc whiterun");
	eca::Config config;
	eca::ViewModel viewModel(candidates, config, history);

	auto view = viewModel.OnInput("player.additem Gol", "tab");
	assert(view.setEntry);
	assert(view.entryText == "player.additem Gold001");
	assert(view.listLines.size() == 2);

	view = viewModel.OnInput("player.setav arch", "tab");
	assert(view.setEntry);
	assert(view.entryText == "player.setav Marksman");

	view = viewModel.OnInput("coc", "ctrlr");
	assert(view.ghost.find("coc whiterun") != std::string::npos);
	view = viewModel.OnInput("coc", "tab");
	assert(view.setEntry);
	assert(view.entryText == "coc whiterun");

	view = viewModel.OnInput("", "ctrlr");
	assert(view.ghost.find("coc whiterun") != std::string::npos);
	view = viewModel.OnInput("", "ctrlr");
	assert(view.ghost.find("help gold") != std::string::npos);
	view = viewModel.OnInput("", "shiftctrlr");
	assert(view.ghost.find("coc whiterun") != std::string::npos);

	return 0;
}
