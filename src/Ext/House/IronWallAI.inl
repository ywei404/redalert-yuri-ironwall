// Compiled once by Body.cpp; no new .vcxproj entry or extension-layout change.
#include <InfantryClass.h>
#include <TeamClass.h>
#include <Memory.h>

namespace IronWallAI
{
	namespace
	{
		constexpr const char* BunkerCrewID = "IW26TYBNK";
		constexpr const char* ReactorCrewID = "IW26TPOWER";

		bool Enabled(HouseClass* house)
		{
			return house && !house->IsControlledByHuman() && !house->Defeated
				&& !house->Type->MultiplayPassive && TeamTypeClass::Find(BunkerCrewID);
		}

		bool Usable(BuildingClass* building)
		{
			return building && building->IsAlive && building->Health > 0
				&& !building->InLimbo && building->IsOnMap
				&& building->GetCurrentMission() != Mission::Construction
				&& building->GetCurrentMission() != Mission::Selling;
		}

		int EmptySeats(HouseClass* house, bool reactor)
		{
			int result = 0;
			for (auto building : house->Buildings)
			{
				if (!Usable(building))
					continue;
				if (reactor)
				{
					if (!_stricmp(building->Type->ID, "YAPOWR"))
						result += std::max(0, building->Type->Passengers - building->Passengers.GetTotalSize());
				}
				else if (building->Type->Bunker && !building->BunkerLinkedItem)
				{
					++result;
				}
			}
			return result;
		}

		bool CanProduce(HouseClass* house, TechnoTypeClass* type)
		{
			return type && house->CanBuild(type, false, false) == CanBuildResult::Buildable
				&& type->FindFactory(true, true, true, house);
		}

		bool Spare(FootClass* unit, HouseClass* house, TechnoTypeClass* type)
		{
			return unit && unit->Owner == house && unit->GetTechnoType() == type
				&& unit->IsAlive && unit->Health > 0 && !unit->InLimbo && !unit->Absorbed
				&& !unit->BunkerLinkedItem && !unit->Team && unit->CanBeRecruited(house)
				&& (unit->GetCurrentMission() == Mission::Guard
					|| unit->GetCurrentMission() == Mission::Area_Guard);
		}

		struct CrewState
		{
			std::vector<TeamClass*> Waiting;
			std::vector<TeamClass*> Finished;
			int Travelling = 0;
			int OldestAge = 0;
		};

		CrewState InspectCrews(HouseClass* house, TeamTypeClass* type)
		{
			CrewState state;
			for (auto team : TeamClass::Array)
			{
				if (team->Owner != house || team->Type != type)
					continue;
				state.OldestAge = std::max(state.OldestAge, Unsorted::CurrentFrame - team->CreationFrame);
				if (!team->FirstUnit)
				{
					if (team->IsHasBeen || team->IsForcedActive)
						state.Finished.push_back(team);
					else
						state.Waiting.push_back(team);
				}
				else
				{
					for (auto unit = team->FirstUnit; unit; unit = unit->NextTeamMember)
						if (unit->IsAlive && unit->Health > 0 && !unit->Absorbed && !unit->BunkerLinkedItem)
							++state.Travelling;
				}
			}
			return state;
		}

		// Native teams/scripts remain responsible for walking, entering and retrying.
		// Allocate only real vacancies, and keep at most ONE unfilled request.
		void SupplyCrews(HouseClass* house, bool reactor)
		{
			auto type = TeamTypeClass::Find(reactor ? ReactorCrewID : BunkerCrewID);
			TechnoTypeClass* unitType = reactor
				? static_cast<TechnoTypeClass*>(InfantryTypeClass::Find("INIT"))
				: static_cast<TechnoTypeClass*>(UnitTypeClass::Find("YTNK"));
			if (!type || !unitType)
				return;
			auto state = InspectCrews(house, type);
			for (auto team : state.Finished) GameDelete(team);
			int slots = EmptySeats(house, reactor);
			int freeSlots = Policy::UnreservedSeats(slots, state.Travelling);

			// Remove obsolete EMPTY orders after a bunker/reactor is lost or filled.
			// Never delete an occupied team or an actual combat unit.
			while (!state.Waiting.empty() && (freeSlots == 0 || state.Waiting.size() > 1))
			{
				auto team = state.Waiting.back();
				state.Waiting.pop_back();
				GameDelete(team);
			}

			std::vector<FootClass*> spares;
			if (reactor)
			{
				for (auto unit : InfantryClass::Array)
					if (Spare(unit, house, unitType)) spares.push_back(unit);
			}
			else
			{
				for (auto unit : UnitClass::Array)
					if (Spare(unit, house, unitType)) spares.push_back(unit);
			}

			for (auto unit : spares)
			{
				if (freeSlots <= 0) break;
				auto team = state.Waiting.empty() ? type->CreateTeam(house) : state.Waiting.back();
				if (!team) break;
				if (!state.Waiting.empty()) state.Waiting.pop_back();
				if (team->AddMember(unit, true))
				{
					--freeSlots;
					++state.Travelling;
				}
				else
				{
					state.Waiting.push_back(team);
					break;
				}
			}

			if (Policy::RequestCrew(slots, state.Travelling, static_cast<int>(state.Waiting.size()))
				&& CanProduce(house, unitType))
			{
				if (auto team = type->CreateTeam(house)) state.Waiting.push_back(team);
			}

			// Fill existing reactors before adding further field infantry requests.
			// Do not replace an order that is already in progress.
			if (reactor && !state.Waiting.empty() && house->ProducingInfantryTypeIndex == -1
				&& CanProduce(house, unitType))
				house->ProducingInfantryTypeIndex = unitType->GetArrayIndex();
		}
	}

	void UpdateHouse(HouseClass* house)
	{
		if (!Enabled(house)) return;
		if (house->Balance < Policy::CreditReserve)
			house->GiveMoney(Policy::CreditReserve - house->Balance);
		if (house->SideIndex != 2) return;

		// Yuri's power expansion responds to actual demand rather than a surplus goal.
		house->PowerSurplus = 0;
		if (!house->Production || Unsorted::CurrentFrame % 15 != house->ArrayIndex % 15)
			return;
		SupplyCrews(house, true);
		SupplyCrews(house, false);
	}

	void InterleaveBunkerProduction(HouseClass* house, const std::vector<int>& needs,
		const std::vector<int>& creationFrames)
	{
		if (!Enabled(house) || house->SideIndex != 2) return;
		auto crewType = TeamTypeClass::Find(BunkerCrewID);
		auto tank = UnitTypeClass::Find("YTNK");
		if (!tank || !crewType || InspectCrews(house, crewType).Waiting.empty()) return;
		int index = tank->ArrayIndex;
		if (index < 0 || static_cast<size_t>(index) >= needs.size() || needs[index] <= 0) return;
		int other = -1;
		int oldest = 0x7fffffff;
		for (int i = 0; i < UnitTypeClass::Array.Count; ++i)
		{
			auto type = UnitTypeClass::Array[i];
			if (i == index || type->Naval || needs[i] <= 0 || !CanProduce(house, type)) continue;
			if (other < 0 || creationFrames[i] < oldest) { other = i; oldest = creationFrames[i]; }
		}
		house->ProducingUnitTypeIndex = Policy::ChooseVehicle(house->ProducingUnitTypeIndex,
			index, other, house->LastBuiltVehicleType, true, CanProduce(house, tank));
	}

	void PrioritizeReactorInfantry(HouseClass* house)
	{
		if (!Enabled(house) || house->SideIndex != 2 || house->ProducingInfantryTypeIndex != -1)
			return;
		auto crewType = TeamTypeClass::Find(ReactorCrewID);
		auto recruit = InfantryTypeClass::Find("INIT");
		if (!crewType || !recruit || InspectCrews(house, crewType).Waiting.empty()
			|| EmptySeats(house, true) <= 0 || !CanProduce(house, recruit)) return;
		for (auto unit : InfantryClass::Array)
			if (Spare(unit, house, recruit)) return; // use existing clones before spending another batch
		house->ProducingInfantryTypeIndex = recruit->ArrayIndex;
	}

	bool WaitBeforeAddingPowerPlant(HouseClass* house, BuildingTypeClass* nextBuilding)
	{
		if (!Enabled(house) || house->SideIndex != 2 || !nextBuilding || nextBuilding->PowerDrain <= 0)
			return false;
		auto crewType = TeamTypeClass::Find(ReactorCrewID);
		auto recruit = InfantryTypeClass::Find("INIT");
		if (!crewType || !recruit) return false;
		// A power shortage should not have to wait for the periodic crew scan.
		SupplyCrews(house, true);
		auto crews = InspectCrews(house, crewType);
		// If the engine cannot allocate a team, do not wait forever on no order.
		if (crews.Waiting.empty() && crews.Travelling == 0) return false;
		// If recruitment/entry is blocked for two minutes, allow emergency expansion.
		return Policy::WaitForReactorCrew(EmptySeats(house, true),
			house->CanBuild(recruit, false, false) == CanBuildResult::Buildable,
			recruit->FindFactory(true, true, true, house) != nullptr,
			crews.OldestAge);
	}
}
