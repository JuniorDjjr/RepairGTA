#include "plugin.h"
#include "CRadar.h"
#include "TestCheat.h"
#include "CTheCarGenerators.h"

using namespace plugin;
using namespace std;

fstream lg;

int runOnceProcessingScriptsCounter = 0;

// Radar icons
tRadarTrace *radarTracesArray;
uint32_t radarTracesTotal = 0;

// Car generators
CCarGenerator *carGeneratorArray;
uint16_t carGeneratorTotal = 0;

CdeclEvent <AddressList<0x5D14D5, H_CALL>, PRIORITY_BEFORE, ArgPickNone, void()> savingEvent;
CdeclEvent <AddressList<0x5D19CE, H_CALL>, PRIORITY_AFTER, ArgPickNone, void()> loadingEvent;

class RepairGTA
{
public:
    RepairGTA()
	{
		// Mod stuff
		lg.open("RepairGTA.log", fstream::out | fstream::trunc);
		lg << "Build 2" << endl;
		lg.flush();

		///////////////////////////////////////////////

		Events::initScriptsEvent += []
		{
			radarTracesArray = (tRadarTrace*)(injector::ReadMemory<uintptr_t>(0x582829 + 4, true) - 0x14);
			radarTracesTotal = injector::ReadMemory<uint32_t>(0x58384C + 2, true);
			carGeneratorArray = (CCarGenerator*)(injector::ReadMemory<uintptr_t>(0x5D3A10 + 2, true));
			carGeneratorTotal = injector::ReadMemory<uint16_t>(0x5D3A04 + 2, true);
		};

		savingEvent += []
		{
			lg << "Saving" << endl;
			RunFixes();
		};

		loadingEvent += []
		{
			lg << "Loading" << endl;
			runOnceProcessingScriptsCounter = 1;
		};

		Events::processScriptsEvent.after += []
		{
			// Manually delete near blips
			if (TestCheat("DELBLIP"))
			{
				CVector2D playerPos = FindPlayerPed(-1)->GetPosition();
				CVector2D blipPos;
				for (unsigned int i = 0; i < radarTracesTotal; i++)
				{
					if (radarTracesArray[i].m_bTrackingBlip && DistanceBetweenPoints(playerPos, radarTracesArray[i].m_vPosition) < 10.0f)
					{
						lg << "Radar Icons: Manually deleted, with type: " << (int)radarTracesArray[i].m_nBlipType <<
							" x: " << radarTracesArray[i].m_vPosition.x <<
							" y: " << radarTracesArray[i].m_vPosition.y <<
							" z: " << radarTracesArray[i].m_vPosition.z << endl;
						CRadar::ClearActualBlip(i);
					}
				}
				lg.flush();
			}

			// Run after first process
			if (runOnceProcessingScriptsCounter == 1)
			{
				runOnceProcessingScriptsCounter = 2;
			}
			else {
				if (runOnceProcessingScriptsCounter == 2)
				{
					lg << "Running after scripts" << endl;
					RunFixes();
					runOnceProcessingScriptsCounter = 0;
				}
			}
		};

    }

	static void RunFixes()
	{
		// Automatically delete duplicated radar icons
		for (uint32_t i = 0; i < radarTracesTotal; i++)
		{
			if (radarTracesArray[i].m_bTrackingBlip)
			{
				for (uint32_t j = 0; j < radarTracesTotal; j++)
				{
					if (i != j && radarTracesArray[j].m_bTrackingBlip &&
						radarTracesArray[i].m_nBlipType == radarTracesArray[j].m_nBlipType &&
						(radarTracesArray[i].m_vPosition.x == radarTracesArray[j].m_vPosition.x) &&
						(radarTracesArray[i].m_vPosition.y == radarTracesArray[j].m_vPosition.y) &&
						(radarTracesArray[i].m_vPosition.z == radarTracesArray[j].m_vPosition.z))
					{
						lg << "Radar Icons: Fixed duplication. Type: " << (int)radarTracesArray[j].m_nBlipType <<
							" x: " << radarTracesArray[j].m_vPosition.x <<
							" y: " << radarTracesArray[j].m_vPosition.y <<
							" z: " << radarTracesArray[j].m_vPosition.z << endl;
						CRadar::ClearActualBlip(j);
					}
				}
			}
		}

		// Automatically delete duplicated car generators
		for (uint32_t i = 0; i < carGeneratorTotal; i++)
		{
			if (carGeneratorArray[i].m_bIsUsed)
			{
				for (uint32_t j = 0; j < carGeneratorTotal; j++)
				{
					if (i != j && carGeneratorArray[j].m_bIsUsed &&
						carGeneratorArray[i].m_nModelId == carGeneratorArray[j].m_nModelId &&
						!carGeneratorArray[i].m_nIplId && !carGeneratorArray[j].m_nIplId &&
						(carGeneratorArray[i].m_nGenerateCount == carGeneratorArray[j].m_nGenerateCount) &&
						(carGeneratorArray[i].m_vecPosn.x == carGeneratorArray[j].m_vecPosn.x) &&
						(carGeneratorArray[i].m_vecPosn.y == carGeneratorArray[j].m_vecPosn.y) &&
						(carGeneratorArray[i].m_vecPosn.z == carGeneratorArray[j].m_vecPosn.z))
					{
						lg << "Car Generators: Fixed duplication. Model: " << (int)carGeneratorArray[j].m_nModelId <<
							" x: " << carGeneratorArray[j].m_vecPosn.x / 8 <<
							" y: " << carGeneratorArray[j].m_vecPosn.y / 8 <<
							" z: " << carGeneratorArray[j].m_vecPosn.z / 8;
						ShowCarGeneratorConsiderationMessageIfNeeded(carGeneratorArray[j]);
						lg << endl;
						carGeneratorArray[j].SwitchOff();
						carGeneratorArray[j].m_bIsUsed = false;
						CTheCarGenerators::NumOfCarGenerators--;
					}
				}
			}
		}

		lg << "Finished" << endl;
		lg.flush();
	}

	static void ShowCarGeneratorConsiderationMessageIfNeeded(CCarGenerator carGenerator)
	{
		bool show = false;
		switch (carGenerator.m_vecPosn.x)
		{
			case 17735:
				if (carGenerator.m_nModelId == 492 && carGenerator.m_vecPosn.y == -9283 && carGenerator.m_vecPosn.z == 197) show = true;
				break;
			case 19265:
				if (carGenerator.m_nModelId == 545 && carGenerator.m_vecPosn.y == -13755 && carGenerator.m_vecPosn.z == 109) show = true;
				break;
			case -14225:
				if (carGenerator.m_nModelId == 557 && carGenerator.m_vecPosn.y == 9656 && carGenerator.m_vecPosn.z == 201) show = true;
				break;
			case -11197:
				if (carGenerator.m_nModelId == 599 && carGenerator.m_vecPosn.y == 21028 && carGenerator.m_vecPosn.z == 446) show = true;
				break;
			case -3036:
				if (carGenerator.m_nModelId == 568 && carGenerator.m_vecPosn.y == -11544 && carGenerator.m_vecPosn.z == 205) show = true;
				break;
			case -20576:
				if (carGenerator.m_nModelId == 442 && carGenerator.m_vecPosn.y == 9188 && carGenerator.m_vecPosn.z == 445) show = true;
				break;
			case 16227:
				if (carGenerator.m_nModelId == 589 && carGenerator.m_vecPosn.y == 21848 && carGenerator.m_vecPosn.z == 84) show = true;
				break;
		}
		if (show)
		{
			lg << " - Maybe original game bug";
		}
	}

} repairGTA;
