#pragma once

// Pure decisions kept separate from game memory for regression tests.
namespace IronWallAI::Policy
{
	constexpr int CreditReserve = 1000000;
	constexpr int CrewTimeoutFrames = 1800; // 120 seconds at 15 game frames/sec.

	constexpr int UnreservedSeats(int emptySeats, int crewsOnTheWay)
	{
		return emptySeats > crewsOnTheWay ? emptySeats - crewsOnTheWay : 0;
	}

	constexpr bool RequestCrew(int emptySeats, int crewsOnTheWay, int waitingTeams)
	{
		return UnreservedSeats(emptySeats, crewsOnTheWay) > 0 && waitingTeams == 0;
	}

	constexpr int ChooseVehicle(int normalChoice, int gattling, int otherChoice,
		int lastBuilt, bool crewNeedsGattling, bool canBuildGattling)
	{
		if (!crewNeedsGattling || !canBuildGattling)
			return normalChoice;
		if (lastBuilt == gattling && otherChoice >= 0)
			return otherChoice;
		return gattling;
	}

	constexpr bool WaitForReactorCrew(int emptySeats, bool canTrain,
		bool hasWorkingBarracks, int oldestCrewAge)
	{
		return emptySeats > 0 && canTrain && hasWorkingBarracks
			&& oldestCrewAge < CrewTimeoutFrames;
	}
}
