#include "plugin.h"
#include "CRadar.h"
#include "TestCheat.h"
#include "CTheCarGenerators.h"
#include "..\injector\assembly.hpp"

using namespace plugin;
using namespace std;
using namespace injector;

fstream lg;

bool lastLoadedObjectDidntExist = false;
bool lastLoadedObjectDidntExistCheck = true;
int lastLoadedObjectWithWrongModel = -1;
int runOnceProcessingScriptsCounter = 0;
int maxDffIndex = 20000;
uintptr_t ORIGINAL_CWorld__Add = 0;
uintptr_t ORIGINAL_LoadObjectPool_CObjectConstructor = 0;

// Radar icons
tRadarTrace *radarTracesArray;
uint32_t radarTracesTotal = 0;

// Car generators
CCarGenerator *carGeneratorArray;
uint16_t carGeneratorTotal = 0;

CdeclEvent <AddressList<0x5D14D5, H_CALL>, PRIORITY_BEFORE, ArgPickNone, void()> savingEvent;
CdeclEvent <AddressList<0x5D19CE, H_CALL>, PRIORITY_AFTER, ArgPickNone, void()> loadingEvent;
//CdeclEvent <AddressList<0x53C6DB, H_CALL>, PRIORITY_AFTER, ArgPickNone, void()> restartEvent;

CObject* __fastcall Custom_LoadObjectPool_CObjectConstructor(CObject* _this, int a2, char a3)
{
	return plugin::CallMethodAndReturnDynGlobal<CObject*, CObject, int, int, char>(ORIGINAL_LoadObjectPool_CObjectConstructor, *_this, 0, a2, a3);
	//return _this->Create(a2);
}

void __cdecl Custom_CWorld__Add(CEntity* entity)
{
	if (lastLoadedObjectDidntExist)
	{
		lg << "Objects: This model isn't installed. Deleted to fix game crash: " << lastLoadedObjectWithWrongModel << endl;
		lastLoadedObjectDidntExistCheck = true;
		CObject* obj = reinterpret_cast<CObject*>(entity);
		delete obj;
	}
	else
	{
		plugin::CallDynGlobal<CEntity*>(ORIGINAL_CWorld__Add, entity);
	}
}

bool ModelExists(int model) {
	if (model >= maxDffIndex || model <= 0 || plugin::CallAndReturn<bool, 0x407800, int>(model) == false) {
		return false;
	}
	return true;
}

int GetAvailableDummyModel() {
	if (ModelExists(1559)) return 1559; // diamond, safe because even total conversions didn't remove it and game system itself uses it
	if (ModelExists(1277)) return 1277; // save pickup, safe because even total conversions didn't remove it
	if (ModelExists(1000)) return 1000; // tuning spoiler, safe because even total conversions didn't remove it
	if (ModelExists(1599)) return 1599; // a fish, safe because even total conversions didn't remove it and game system itself uses i
	// almost impossible, but if not found, search manually on every map model ID
	for (int i = 4000; i < 10000; ++i) {
		if (ModelExists(i)) return i;
	}
	if (ModelExists(346)) return 346; // pistol, safe but not totally (near peds ids, maybe total conversion change it)
	if (ModelExists(712)) return 712; // a tree, safe but not totally (near vehicles ids, maybe total conversion change it)
	// I give it up, just crashes it
	return 0;
}

class RepairGTA
{
public:
    RepairGTA()
	{
		// Mod stuff
		lg.open("RepairGTA.log", fstream::out | fstream::trunc);
		lg << "Version: Build 3" << endl;
		lg.flush();

		///////////////////////////////////////////////

		maxDffIndex = ReadMemory<int>(0x407104 + 2, true);
		if (maxDffIndex < 1000) maxDffIndex = 20000;
		ORIGINAL_LoadObjectPool_CObjectConstructor = ReadMemory<uintptr_t>(0x5D4AEC + 1, true);
		ORIGINAL_LoadObjectPool_CObjectConstructor += (GetGlobalAddress(0x5D4AEC) + 5);

		// What we do here:
		// Give a temp dummy model ID if loaded object didn't exist
		// Make the game keep loading that object, so it is not intrusive
		// Instead of add the object to the world, patch it to delete it
		// But also do a check to see if the object is being really removed
		MakeInline<0x5D4A8C, 0x5D4A8C + 6>([](injector::reg_pack& regs)
		{
			regs.edx = *(uintptr_t*)0xB7449C; //mov     edx, _ZN6CPools14ms_pObjectPoolE ; CPools::ms_pObjectPool
			if (!lastLoadedObjectDidntExistCheck) {
				return;
			}
			int modelIndex = *(int*)(regs.esp + 0x60 - 0x3C);
			// Check model exists
			if (ModelExists(modelIndex) == false) {
				
				*(int*)(regs.esp + 0x60 - 0x3C) = GetAvailableDummyModel();
				// after load, delete it instead of add to the world
				lastLoadedObjectDidntExist = true;
				lastLoadedObjectWithWrongModel = modelIndex;
				// flag to check if the delete was a sucess, if not (some other mod patched my call), keep doing this, it's better to just crash the game instead of corrupt the saved game with a lot of dummy objects
				lastLoadedObjectDidntExistCheck = false;
			}
			else {
				lastLoadedObjectDidntExist = false;
				lastLoadedObjectWithWrongModel = -1;
			}
		});

		ORIGINAL_CWorld__Add = ReadMemory<uintptr_t>(0x5D4B1D + 1, true);
		ORIGINAL_CWorld__Add += (GetGlobalAddress(0x5D4B1D) + 5);
		patch::RedirectCall(0x5D4B1D, Custom_CWorld__Add);

		Events::initScriptsEvent += []
		{
			radarTracesArray = (tRadarTrace*)(injector::ReadMemory<uintptr_t>(0x582829 + 4, true) - 0x14);
			radarTracesTotal = injector::ReadMemory<uint32_t>(0x58384C + 2, true);
			carGeneratorArray = (CCarGenerator*)(injector::ReadMemory<uintptr_t>(0x5D3A10 + 2, true));
			carGeneratorTotal = injector::ReadMemory<uint16_t>(0x5D3A04 + 2, true);
		};

		savingEvent += []
		{
			lg << "Before Saving" << endl;
			RunFixes();
		};

		loadingEvent += []
		{
			lg << "After Loading" << endl;
			runOnceProcessingScriptsCounter = 1;
		};

		Events::processScriptsEvent.after += []
		{
			// Manually delete near blips
			if (TestCheat("DELBLIP"))
			{
				CVector2D playerPos = FindPlayerPed(-1)->GetPosition();
				CVector2D blipPos;
				int closestObject = -1;
				float closestDistance = 999999.0f;
				for (unsigned int i = 0; i < radarTracesTotal; i++)
				{
					if (radarTracesArray[i].m_bTrackingBlip)
					{
						float distance = DistanceBetweenPoints(playerPos, radarTracesArray[i].m_vPosition);
						if (distance < 10.0f && distance < closestDistance) {
							closestDistance = distance;
							closestObject = i;
						}
					}
				}
				if (closestObject > -1)
				{
					lg << "Radar Icons: Manually deleted, with type: " << (int)radarTracesArray[closestObject].m_nBlipType <<
						" x: " << radarTracesArray[closestObject].m_vPosition.x <<
						" y: " << radarTracesArray[closestObject].m_vPosition.y <<
						" z: " << radarTracesArray[closestObject].m_vPosition.z << endl;
					CRadar::ClearActualBlip(closestObject);
				}
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
