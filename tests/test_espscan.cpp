#include "EspScan.h"

#ifdef NDEBUG
#	undef NDEBUG
#endif
#include <cassert>

int main(int a_count, char** a_arguments)
{
	assert(a_count == 2);
	std::vector<std::string> masters;
	bool light = false;
	std::string error;
	assert(eca::esp::ReadMasters(a_arguments[1], masters, light, error));
	assert(masters.empty());
	assert(!light);

	const std::set<std::string> wanted{ "WEAP", "CELL", "QUST", "GMST", "RACE", "SHOU" };
	eca::esp::ScanResult result;
	assert(eca::esp::ScanFile(a_arguments[1], { 0 }, wanted, result, error));
	assert(result.records.size() > 100);
	assert(!result.questStages.empty());
	return 0;
}
