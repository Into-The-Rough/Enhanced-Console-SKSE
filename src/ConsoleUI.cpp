#include "ConsoleUI.h"

#include "Engine.h"

#include <array>
#include <exception>

namespace eca::consoleui
{
	namespace
	{
		using ProcessMessage = RE::UI_MESSAGE_RESULTS (*)(RE::Console*, RE::UIMessage&);

		constexpr std::size_t kProcessMessageSlot = 4;
		constexpr std::array<const char*, 4> kConsolePaths{
			"_global.Console.ConsoleInstance",
			"_root.consoleFader_mc.Console_mc",
			"_root.Console_mc",
			"_root.Console"
		};
		ProcessMessage g_original{ nullptr };
		bool g_hookInstalled{ false };
		bool g_controlDown{ false };
		bool g_shiftDown{ false };
		bool g_historySearch{ false };
		bool g_fieldResultLogged{ false };
		std::uint32_t g_loggedInputEvents{ 0 };
		const char* g_resolvedConsolePath{ nullptr };

		ViewModel& GetViewModel()
		{
			static ViewModel model(engine::GetCandidates(), engine::GetConfig(), engine::GetHistory());
			return model;
		}

		double GetNumber(const RE::GFxValue& a_object, const char* a_name, double a_default = 0.0)
		{
			RE::GFxValue value;
			return a_object.GetMember(a_name, &value) && value.IsNumber() ? value.GetNumber() : a_default;
		}

		bool FindConsoleObjects(RE::GFxMovieView* a_movie, RE::GFxValue& a_console, RE::GFxValue& a_entry)
		{
			if (!a_movie) {
				return false;
			}
			if (g_resolvedConsolePath && a_movie->GetVariable(&a_console, g_resolvedConsolePath) &&
				a_console.GetMember("CommandEntry", &a_entry)) {
				return true;
			}
			for (const auto* path : kConsolePaths) {
				if (a_movie->GetVariable(&a_console, path) && a_console.GetMember("CommandEntry", &a_entry)) {
					g_resolvedConsolePath = path;
					SKSE::log::info("console UI: resolved command entry through {}", path);
					return true;
				}
			}
			return false;
		}

		std::string GetEntryText(RE::GFxMovieView* a_movie)
		{
			if (!a_movie) {
				return {};
			}
			RE::GFxValue console;
			RE::GFxValue entry;
			RE::GFxValue text;
			if (!FindConsoleObjects(a_movie, console, entry) || !entry.GetMember("text", &text) || !text.IsString()) {
				return {};
			}
			return text.GetString();
		}

		void SetEntryText(RE::GFxMovieView* a_movie, std::string_view a_text, int a_caret)
		{
			if (!a_movie) {
				return;
			}
			RE::GFxValue console;
			RE::GFxValue entry;
			if (!FindConsoleObjects(a_movie, console, entry)) {
				return;
			}
			entry.SetText(std::string(a_text).c_str());
			std::array<RE::GFxValue, 2> selection{
				RE::GFxValue(a_caret),
				RE::GFxValue(a_caret)
			};
			a_movie->Invoke("_global.Selection.setSelection", nullptr, selection.data(),
				static_cast<std::uint32_t>(selection.size()));
		}

		bool CreateField(RE::GFxValue& a_console, RE::GFxValue& a_entry,
			const char* a_name, int a_depth, double a_alpha)
		{
			RE::GFxValue existing;
			if (a_console.GetMember(a_name, &existing) && existing.IsDisplayObject()) {
				return true;
			}
			const double x = GetNumber(a_entry, "_x", 8.0);
			const double y = GetNumber(a_entry, "_y", 700.0);
			const double width = GetNumber(a_entry, "_width", 1000.0);
			const double height = std::max(20.0, GetNumber(a_entry, "_height", 24.0));
			std::array<RE::GFxValue, 6> arguments{
				RE::GFxValue(a_name),
				RE::GFxValue(a_depth),
				RE::GFxValue(x),
				RE::GFxValue(y - height),
				RE::GFxValue(width),
				RE::GFxValue(height)
			};
			if (!a_console.Invoke("createTextField", nullptr, arguments.data(),
				static_cast<std::uint32_t>(arguments.size()))) {
				return false;
			}
			RE::GFxValue field;
			if (!a_console.GetMember(a_name, &field)) {
				return false;
			}
			field.SetMember("autoSize", RE::GFxValue("left"));
			field.SetMember("multiline", RE::GFxValue(true));
			field.SetMember("wordWrap", RE::GFxValue(false));
			field.SetMember("selectable", RE::GFxValue(false));
			field.SetMember("noTranslate", RE::GFxValue(true));
			field.SetMember("_alpha", RE::GFxValue(a_alpha));
			RE::GFxValue format;
			if (a_entry.Invoke("getNewTextFormat", &format)) {
				std::array<RE::GFxValue, 1> formatArgument{ format };
				field.Invoke("setNewTextFormat", nullptr, formatArgument.data(), 1);
				field.Invoke("setTextFormat", nullptr, formatArgument.data(), 1);
			}
			return true;
		}

		bool EnsureFields(RE::GFxMovieView* a_movie, RE::GFxValue& a_console,
			RE::GFxValue& a_entry, RE::GFxValue& a_ghost, RE::GFxValue& a_list)
		{
			if (!a_movie) {
				if (!g_fieldResultLogged) {
					SKSE::log::warn("console UI: movie is unavailable");
					g_fieldResultLogged = true;
				}
				return false;
			}
			if (!FindConsoleObjects(a_movie, a_console, a_entry)) {
				if (!g_fieldResultLogged) {
					SKSE::log::warn("console UI: command entry missing at every known path");
					g_fieldResultLogged = true;
				}
				return false;
			}
			if (!CreateField(a_console, a_entry, "EnhancedConsoleGhost", 32000, 55.0) ||
				!CreateField(a_console, a_entry, "EnhancedConsoleList", 32001, 90.0)) {
				if (!g_fieldResultLogged) {
					SKSE::log::warn("console UI: failed to create suggestion fields");
					g_fieldResultLogged = true;
				}
				return false;
			}
			const bool foundFields = a_console.GetMember("EnhancedConsoleGhost", &a_ghost) &&
				a_console.GetMember("EnhancedConsoleList", &a_list);
			if (!g_fieldResultLogged) {
				SKSE::log::info("console UI: suggestion fields {}", foundFields ? "ready" : "missing after creation");
				g_fieldResultLogged = true;
			}
			return foundFields;
		}

		void ApplyView(RE::GFxMovieView* a_movie, const ViewState& a_view)
		{
			if (!a_movie) {
				return;
			}
			if (a_view.setEntry) {
				SetEntryText(a_movie, a_view.entryText, a_view.caretPos);
			}
			RE::GFxValue console;
			RE::GFxValue entry;
			RE::GFxValue ghost;
			RE::GFxValue list;
			if (!EnsureFields(a_movie, console, entry, ghost, list)) {
				return;
			}

			std::string listText = a_view.counter;
			for (const auto& line : a_view.listLines) {
				if (!listText.empty()) {
					listText += '\n';
				}
				listText += line;
			}
			ghost.SetText(a_view.ghost.c_str());
			list.SetText(listText.c_str());
			ghost.SetMember("_visible", RE::GFxValue(!a_view.ghost.empty()));
			list.SetMember("_visible", RE::GFxValue(!listText.empty()));

			const double entryX = GetNumber(entry, "_x", 8.0);
			const double entryY = GetNumber(entry, "_y", 700.0);
			const double lineHeight = std::max(18.0, GetNumber(entry, "_height", 24.0));
			const double ghostHeight = std::max(lineHeight, GetNumber(ghost, "_height", lineHeight));
			const double ghostY = entryY - ghostHeight - 1.0;
			ghost.SetMember("_x", RE::GFxValue(entryX));
			ghost.SetMember("_y", RE::GFxValue(ghostY));
			const double ghostTop = a_view.ghost.empty() ? entryY : ghostY;
			const double estimatedListHeight = lineHeight * static_cast<double>(a_view.listLines.size() +
				(a_view.counter.empty() ? 0 : 1));
			const double listHeight = listText.empty() ? 0.0 :
				std::max(estimatedListHeight, GetNumber(list, "_height", estimatedListHeight));
			const double listY = ghostTop - listHeight - (listText.empty() ? 0.0 : 2.0);
			list.SetMember("_x", RE::GFxValue(entryX));
			list.SetMember("_y", RE::GFxValue(listY));

			RE::GFxValue history;
			if (console.GetMember("CommandHistory", &history)) {
				const double historyY = GetNumber(history, "_y", 0.0);
				const double overlayTop = !listText.empty() ? listY : (!a_view.ghost.empty() ? ghostY : entryY);
				const double historyHeight = std::max(lineHeight, overlayTop - historyY - 2.0);
				history.SetMember("_height", RE::GFxValue(historyHeight));
				RE::GFxValue maximumScroll;
				if (history.GetMember("maxscroll", &maximumScroll) && maximumScroll.IsNumber()) {
					history.SetMember("scroll", maximumScroll);
				}
			}
		}

		void UpdateFromEntry(RE::GFxMovieView* a_movie)
		{
			ApplyView(a_movie, GetViewModel().OnInput(GetEntryText(a_movie), "change"));
		}

		bool Modifier(const RE::GFxKeyEvent& a_event, RE::GFxSpecialKeysState::Key a_key)
		{
			return (a_event.specialKeyState.states & static_cast<std::uint8_t>(a_key)) != 0;
		}

		RE::UI_MESSAGE_RESULTS Hook(RE::Console* a_console, RE::UIMessage& a_message)
		{
			RE::UI_MESSAGE_RESULTS result = RE::UI_MESSAGE_RESULTS::kPassOn;
			bool originalCalled = false;
			try {
				engine::InjectEditorIDsIfReady();
				if (a_message.type == RE::UI_MESSAGE_TYPE::kScaleformEvent && a_message.data) {
					auto* data = static_cast<RE::BSUIScaleformData*>(a_message.data);
					auto* event = data ? data->scaleformEvent : nullptr;
					if (event && g_loggedInputEvents < 20) {
						if (event->type == RE::GFxEvent::EventType::kKeyDown ||
							event->type == RE::GFxEvent::EventType::kKeyUp) {
							const auto* keyEvent = static_cast<RE::GFxKeyEvent*>(event);
							SKSE::log::info("console input event: type {}, key {}",
								static_cast<std::uint32_t>(event->type.underlying()), static_cast<std::uint32_t>(keyEvent->keyCode));
						} else {
							SKSE::log::info("console input event: type {}", static_cast<std::uint32_t>(event->type.underlying()));
						}
						++g_loggedInputEvents;
					}
					if (event && (event->type == RE::GFxEvent::EventType::kKeyDown ||
						event->type == RE::GFxEvent::EventType::kKeyUp)) {
						auto* keyEvent = static_cast<RE::GFxKeyEvent*>(event);
						const bool keyDown = event->type == RE::GFxEvent::EventType::kKeyDown;
						if (keyEvent->keyCode == RE::GFxKey::kControl) {
							g_controlDown = keyDown;
						}
						if (keyEvent->keyCode == RE::GFxKey::kShift) {
							g_shiftDown = keyDown;
						}
						if (keyDown) {
							const bool control = g_controlDown ||
								Modifier(*keyEvent, RE::GFxSpecialKeysState::Key::kCtrlPressed);
							const bool shift = g_shiftDown ||
								Modifier(*keyEvent, RE::GFxSpecialKeysState::Key::kShiftPressed);
							const bool tab = keyEvent->keyCode == RE::GFxKey::kTab && engine::GetConfig().useTab;
							const bool controlSpace = keyEvent->keyCode == RE::GFxKey::kSpace && control;
							if (tab || controlSpace) {
								const bool reverse = shift || (tab && control);
								const auto entry = GetEntryText(a_console->uiMovie.get());
								const auto view = GetViewModel().OnInput(entry, reverse ? "shifttab" : "tab");
								SKSE::log::info("completion key: entry '{}', result '{}', {} matches",
									entry, view.setEntry ? view.entryText : "<none>", view.listLines.size());
								ApplyView(a_console->uiMovie.get(), view);
								g_historySearch = false;
								return RE::UI_MESSAGE_RESULTS::kHandled;
							}
							if (keyEvent->keyCode == RE::GFxKey::kR && control) {
								const char* action = g_historySearch && shift ? "shiftctrlr" : "ctrlr";
								g_historySearch = true;
								ApplyView(a_console->uiMovie.get(), GetViewModel().OnInput(GetEntryText(a_console->uiMovie.get()), action));
								return RE::UI_MESSAGE_RESULTS::kHandled;
							}
							if (keyEvent->keyCode == RE::GFxKey::kUp || keyEvent->keyCode == RE::GFxKey::kDown) {
								std::string recalled;
								const bool moved = keyEvent->keyCode == RE::GFxKey::kUp ?
									engine::RecallOlder(recalled) : engine::RecallNewer(recalled);
								if (moved) {
									SetEntryText(a_console->uiMovie.get(), recalled, static_cast<int>(recalled.size()));
									UpdateFromEntry(a_console->uiMovie.get());
								}
								return RE::UI_MESSAGE_RESULTS::kHandled;
							}
						}
					}
				}

				std::string submitted;
				std::string entryBeforeOriginal;
				bool submit = false;
				bool reset = false;
				const bool compareEntry = a_message.type == RE::UI_MESSAGE_TYPE::kScaleformEvent;
				if (compareEntry) {
					entryBeforeOriginal = GetEntryText(a_console->uiMovie.get());
				}
				if (a_message.type == RE::UI_MESSAGE_TYPE::kScaleformEvent && a_message.data) {
					auto* data = static_cast<RE::BSUIScaleformData*>(a_message.data);
					auto* event = data ? data->scaleformEvent : nullptr;
					if (event && event->type == RE::GFxEvent::EventType::kKeyDown) {
						auto* keyEvent = static_cast<RE::GFxKeyEvent*>(event);
						submit = keyEvent->keyCode == RE::GFxKey::kReturn || keyEvent->keyCode == RE::GFxKey::kKP_Enter;
						reset = submit || keyEvent->keyCode == RE::GFxKey::kEscape || keyEvent->keyCode == RE::GFxKey::kBar;
						if (submit) {
							submitted = GetEntryText(a_console->uiMovie.get());
						}
					}
				}

				result = g_original(a_console, a_message);
				originalCalled = true;
				if (submit && !submitted.empty()) {
					engine::PushHistory(std::move(submitted));
				}
				if (reset) {
					engine::ResetRecall();
					g_historySearch = false;
					ApplyView(a_console->uiMovie.get(), GetViewModel().OnInput({}, "reset"));
				} else if (compareEntry) {
					const auto entryAfterOriginal = GetEntryText(a_console->uiMovie.get());
					if (entryAfterOriginal != entryBeforeOriginal) {
						ApplyView(a_console->uiMovie.get(), GetViewModel().OnInput(entryAfterOriginal, "change"));
					}
				}
				return result;
			} catch (...) {
				return !originalCalled && g_original ? g_original(a_console, a_message) : result;
			}
		}

		void InstallHook()
		{
			if (g_hookInstalled) {
				return;
			}
			auto* ui = RE::UI::GetSingleton();
			const auto menu = ui ? ui->GetMenu<RE::Console>() : nullptr;
			if (!menu) {
				SKSE::log::warn("console hook: menu is unavailable");
				return;
			}
			auto** vtable = *reinterpret_cast<void***>(menu.get());
			void** slot = &vtable[kProcessMessageSlot];
			std::uint32_t oldProtection = 0;
			if (!REX::W32::VirtualProtect(slot, sizeof(void*), REX::W32::PAGE_EXECUTE_READWRITE, &oldProtection)) {
				SKSE::log::warn("console hook: VirtualProtect failed");
				return;
			}
			g_original = reinterpret_cast<ProcessMessage>(*slot);
			*slot = reinterpret_cast<void*>(&Hook);
			REX::W32::VirtualProtect(slot, sizeof(void*), oldProtection, &oldProtection);
			g_hookInstalled = true;
			SKSE::log::info("console input hook installed");
		}

		class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event || a_event->menuName != RE::Console::MENU_NAME) {
					return RE::BSEventNotifyControl::kContinue;
				}
				if (a_event->opening) {
					g_fieldResultLogged = false;
					g_loggedInputEvents = 0;
					g_resolvedConsolePath = nullptr;
					InstallHook();
					engine::InjectEditorIDsIfReady();
					if (auto* ui = RE::UI::GetSingleton()) {
						if (const auto menu = ui->GetMenu<RE::Console>()) {
							UpdateFromEntry(menu->uiMovie.get());
						}
					}
				} else {
					g_controlDown = false;
					g_shiftDown = false;
					g_historySearch = false;
					engine::ResetRecall();
					GetViewModel().OnInput({}, "reset");
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Register()
	{
		static MenuSink sink;
		if (auto* ui = RE::UI::GetSingleton()) {
			ui->AddEventSink(&sink);
			SKSE::log::info("console menu listener registered");
		} else {
			SKSE::log::warn("console menu listener could not be registered");
		}
	}
}
