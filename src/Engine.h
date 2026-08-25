#pragma once

#include "Core.h"

namespace eca::engine
{
	Config& GetConfig();
	ICandidates& GetCandidates();
	HistoryStore& GetHistory();
	void OnDataLoaded();
	bool InjectEditorIDsIfReady();
	void PushHistory(std::string a_command);
	bool RecallOlder(std::string& a_command);
	bool RecallNewer(std::string& a_command);
	void ResetRecall();
}
