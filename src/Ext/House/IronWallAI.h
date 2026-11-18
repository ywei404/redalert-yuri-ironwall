#pragma once
#include <vector>
#include "IronWallAIPolicy.h"

class HouseClass;
class BuildingTypeClass;

namespace IronWallAI
{
	void UpdateHouse(HouseClass* house);
	void PrioritizeReactorInfantry(HouseClass* house);
	void InterleaveBunkerProduction(HouseClass* house, const std::vector<int>& needs,
		const std::vector<int>& creationFrames);
	bool WaitBeforeAddingPowerPlant(HouseClass* house, BuildingTypeClass* nextBuilding);
}
