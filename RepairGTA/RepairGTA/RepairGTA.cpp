#include "plugin.h"
#include "CRadar.h"
#include "TestCheat.h"
#include "CTheCarGenerators.h"
#include "CEntryExit.h"
#include "CEntryExitManager.h"
#include "CClock.h"
#include "CCamera.h"
#include "CMenuManager.h"
#include "CFileMgr.h"
#include "..\injector\assembly.hpp"

using namespace plugin;
using namespace std;
using namespace injector;

string buildVersionName = "5";

fstream lg;
fstream saveExtender;

vector<unsigned int> loadedEnexAttachHashes = vector<unsigned int>();

bool lastLoadedObjectDidntExist = false;
bool lastLoadedObjectDidntExistCheck = true;
int lastLoadedObjectWithWrongModel = -1;
int runOnceProcessingScriptsCounter = 0;
int maxDffIndex = 20000;
int sizeOfEnexStruct = 60;
uintptr_t ORIGINAL_CWorld__Add = 0;
uintptr_t ORIGINAL_LoadObjectPool_CObjectConstructor = 0;

// Radar icons
tRadarTrace *radarTracesArray;
uint32_t radarTracesTotal = 0;

// Car generators
CCarGenerator *carGeneratorArray;
uint16_t carGeneratorTotal = 0;

CdeclEvent <AddressList<0x5D15A6, H_CALL>, PRIORITY_AFTER, ArgPickNone, void()> savingEvent;
CdeclEvent <AddressList<0x5D14D5, H_CALL>, PRIORITY_BEFORE, ArgPickNone, void()> savingEventStart;
CdeclEvent <AddressList<0x5D19CE, H_CALL>, PRIORITY_AFTER, ArgPickNone, void()> loadingEvent;
CdeclEvent <AddressList<0x5D1911, H_CALL>, PRIORITY_BEFORE, ArgPickNone, void()> loadingEventStart;
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
		lg << "Objects: This model isn't installed. Deleted to fix game crash: " << lastLoadedObjectWithWrongModel << "\n";
		lg.flush();
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
	if (ModelExists(1599)) return 1599; // a fish, safe because even total conversions didn't remove it and game system itself uses it
	// almost impossible, but if not found, search manually on every map model ID
	for (int i = 4000; i < 10000; ++i) {
		if (ModelExists(i)) return i;
	}
	if (ModelExists(346)) return 346; // pistol, safe but not totally (near peds ids, maybe total conversion change it)
	if (ModelExists(712)) return 712; // a tree, safe but not totally (near vehicles ids, maybe total conversion change it)
	// I give it up, just crashes it
	return 0;
}

unsigned int BuildEnexHashByPos(CEntryExit* enex) {
	// this is not 100%, but 99,99999% is enough for our use case
	unsigned int nameHash = 0;
	for (int i = 0; i < 8; i++) { 
		if (enex->m_szName[i] == 0) break;
		nameHash += (unsigned int)enex->m_szName[i];
	}

	unsigned int x = abs(enex->m_recEntrance.left);
	unsigned int y = abs(enex->m_recEntrance.top);
	unsigned int z = abs(enex->m_fEntranceZ * 10.0f);

	return (x * 100000) + (y * 1000) + (z * 10) * + nameHash;
}

unsigned long BuildSaveHash() {
	// enough for now (used only if the person changed the save file without deleting the extender file), but if required, change it for file hash or something
	float camPosHash = -1.0f;
	CMatrix* matrix = TheCamera.GetMatrix();
	if (matrix) {
		camPosHash = matrix->pos.x + matrix->pos.y + matrix->pos.z;
	}
	return CClock::ms_nLastClockTick + (CClock::ms_nGameClockSeconds * 1000) + (CClock::ms_nGameClockDays * 10000) + camPosHash;
}

CEntryExit* FindEnexAttachedByHash(CEntryExit* entry) {
	unsigned int entryHash = BuildEnexHashByPos(entry);

	//lg << "Start trying entry hash " << entryHash << "\n";
	int size = loadedEnexAttachHashes.size() - 1;
	for (int i = 0; i < size; i += 2) {
		//lg << "Test " << loadedEnexAttachHashes[i] << "\n";
		if (entryHash == loadedEnexAttachHashes[i]) {
			unsigned int exitHash = loadedEnexAttachHashes[i + 1];
			//lg << "Trying find exit " << exitHash << "\n";
			
			int sizeEnexes = CEntryExitManager::mp_poolEntryExits->m_nSize;
			for (int k = 0; k < sizeEnexes; ++k) {
				if ((CEntryExitManager::mp_poolEntryExits->m_byteMap[k].IntValue() & 0x80) == 0) {
					CEntryExit* exit = (CEntryExit*)((char*)CEntryExitManager::mp_poolEntryExits->m_pObjects + sizeOfEnexStruct * k);
					if (exit && BuildEnexHashByPos(exit) == exitHash) {
						//lg << "FOUND enex attach by hash from loaded file " << entryHash << "\n";
						if (exit == entry) lg << "WARNING: ENEX LINK ATTACHED ITSELF " << entryHash << " pos " << entry->m_recEntrance.left << " " << entry->m_recEntrance.top << " " << entry->m_fEntranceZ << " name " << entry->m_szName << "\n";
						return exit;
					}
				}
			}
		}
	}
	string exitName = "";
	if (entry->m_pLink) {
		exitName = entry->m_pLink->m_szName;
	}
	lg << "Can't find enex link from file (may not be important): " << entryHash << " pos " << entry->m_recEntrance.left << " " << entry->m_recEntrance.top << " " << entry->m_fEntranceZ << " name " << entry->m_szName << " " << exitName << "\n";
	return nullptr;
}

int FindEnexEntryHashIndex(CEntryExit*entry)
{
	unsigned int enexHash = BuildEnexHashByPos(entry);
	int size = loadedEnexAttachHashes.size() - 1;
	for (int i = 0; i < size; i += 2) {
		if (enexHash == loadedEnexAttachHashes[i]) {
			return i;
		}
	}
	return -1;
}

int GetSaveSlot() {
	return FrontEndMenuManager.m_nSelectedSaveGame + 1;
}

class RepairGTA
{
public:
    RepairGTA()
	{
		// Mod stuff
		lg.open("RepairGTA.log", fstream::out | fstream::trunc);
		lg << "Version: Build " << buildVersionName << "\n";
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
			sizeOfEnexStruct = CEntryExitManager::mp_poolEntryExits->GetObjectSize();
			if (sizeOfEnexStruct < 60 || sizeOfEnexStruct > 1000) sizeOfEnexStruct = 60; //something is wrong
		};

		savingEventStart += []
		{
			lg << "Before Saving" << "\n";

			int saveSlot = GetSaveSlot();

			if (saveSlot > 0 && saveSlot <= 8) {
				CFileMgr::SetDirMyDocuments();
				string saveExtenderFileName = "RepairGTASaveExtender" + to_string(saveSlot) + ".dat";

				saveExtender.open(saveExtenderFileName, fstream::out | fstream::trunc);
				if (saveExtender.is_open()) {
					lg << "Saving extender file " << saveExtenderFileName << "\n";
					lg.flush();

					saveExtender << buildVersionName << "\n";
					saveExtender << BuildSaveHash() << "\n";
					saveExtender << "enex" << "\n";

					int sizeEnexes = CEntryExitManager::mp_poolEntryExits->m_nSize;
					for (int i = 0; i < sizeEnexes; ++i) {
						if ((CEntryExitManager::mp_poolEntryExits->m_byteMap[i].IntValue() & 0x80) == 0) {
							CEntryExit* entry = (CEntryExit*)((char*)CEntryExitManager::mp_poolEntryExits->m_pObjects + sizeOfEnexStruct * i);
							if (entry) {
								CEntryExit* exit = entry->m_pLink;
								if (exit) {
									int entryHashIndex = FindEnexEntryHashIndex(entry);
									if (entryHashIndex >= 0) {
										// reput exit, just to make sure it's updated (but this case, if the game is bugged, it will preserve the bug)
										loadedEnexAttachHashes[entryHashIndex + 1] = BuildEnexHashByPos(exit);
									}
									else {
										loadedEnexAttachHashes.push_back(BuildEnexHashByPos(entry));
										loadedEnexAttachHashes.push_back(BuildEnexHashByPos(exit));
									}
								}
							}
						}
					}

					int size = loadedEnexAttachHashes.size() - 1;
					for (int i = 0; i < size; i += 2) {
						saveExtender << loadedEnexAttachHashes[i] << " " << loadedEnexAttachHashes[i+1] << "\n";
					}
				}
				else {
					lg << "Can't open " << saveExtenderFileName << "\n";
				}
				CFileMgr::SetDir("");
			}
			else {
				lg << "WARNING: Can't define save slot: " << saveSlot << "\n";
			}
			lg.flush();
			RunFixes();
		};

		savingEvent += []
		{
			lg << "After Saving" << "\n";
			if (saveExtender.is_open()) {
				saveExtender << "end" << "\n";
				saveExtender.flush();
				saveExtender.close();
			}
			lg.flush();
		};

		loadingEventStart += []
		{
			lg << "Before Loading" << "\n";
			loadedEnexAttachHashes.clear();

			int saveSlot = GetSaveSlot();
			if (saveSlot > 0 && saveSlot <= 8) {
				CFileMgr::SetDirMyDocuments();
				string saveExtenderFileName = "RepairGTASaveExtender" + to_string(saveSlot) + ".dat";
				saveExtender.open(saveExtenderFileName, fstream::in);

				if (saveExtender.is_open())
				{
					lg << "Loading extender file " << saveExtenderFileName << "\n";
					lg.flush();
					string line;
					size_t pos = 0;

					if (getline(saveExtender, line)) // build version, not required for now
					{
						if (getline(saveExtender, line)) // save hash
						{
							if (stoul(line) == BuildSaveHash()) {
								int section = 0;
								while (getline(saveExtender, line))
								{
									//lg << line << "\n";
									if (line.compare("enex") == 0) {
										section = 1;
									}
									else {
										if (section == 1) {
											if ((pos = line.find(" ")) != string::npos) {
												unsigned int entryHash = stoi(line.substr(0, pos));
												unsigned int exitHash = stoi(line.substr(pos));
												loadedEnexAttachHashes.push_back(entryHash);
												loadedEnexAttachHashes.push_back(exitHash);
											}
										}
									}
								}
							}
							else {
								lg << "WARNING: " << saveExtenderFileName << " uses different hash compared to current save file. It will be ignored." << "\n";
							}
						}
						else {
							lg << "Can't detect save hash in " << saveExtenderFileName << "\n";
						}
					}
					else {
						lg << "Can't detect version in " << saveExtenderFileName << "\n";
					}
				}
				else {
					lg << "Can't open " << saveExtenderFileName << "\n";
				}
				CFileMgr::SetDir("");
			}
			else {
				lg << "WARNING: Can't define save slot: " << saveSlot << "\n";
			}
			/*lg << "Start of enex attach hashes:" << "\n";
			for (auto &hash : loadedEnexAttachHashes) {
				lg << hash << "\n";
			}
			lg << "End of enex attach hashes" << "\n";*/
			lg.flush();
			saveExtender.close();
			runOnceProcessingScriptsCounter = 1;
		};
		 
		/*loadingEvent += []
		{
			lg << "After Loading" << "\n";
			lg.flush();
			runOnceProcessingScriptsCounter = 1;
		};*/

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
					if (radarTracesArray[i].m_bInUse)
					{
						float distance = DistanceBetweenPoints(playerPos, radarTracesArray[i].m_vecPos);
						if (distance < 10.0f && distance < closestDistance) {
							closestDistance = distance;
							closestObject = i;
						}
					}
				}
				if (closestObject > -1)
				{
					lg << "Radar Icons: Manually deleted, with type: " << (int)radarTracesArray[closestObject].m_nBlipType <<
						" x: " << radarTracesArray[closestObject].m_vecPos.x <<
						" y: " << radarTracesArray[closestObject].m_vecPos.y <<
						" z: " << radarTracesArray[closestObject].m_vecPos.z << "\n";
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
					lg << "Running after scripts" << "\n";
					RunFixes();
					runOnceProcessingScriptsCounter = 0;
					lg.flush();
				}
			}
		};

		//5D5970 save enex
		/*MakeInline<0x5D59EF, 0x5D59EF + 7>([](injector::reg_pack& regs)
		{
			*(uint32_t*)(regs.esp + 0x10) = regs.ebx; //mov     [esp+18h+pSource], ebx
			regs.eax = *(uint32_t*)(regs.edi + 0x38); //mov     eax, [edi+38h]

			CEntryExit* entry = (CEntryExit * )regs.edi;
			CEntryExit* exit = (CEntryExit * )regs.eax;
			if (exit && saveExtender.is_open()) {
				saveExtender << BuildEnexHashByPos(entry) << " " << BuildEnexHashByPos(exit) << "\n";
			}
		});*/

		//5D55C0 load enex
		MakeInline<0x5D5692, 0x5D56A6>([](injector::reg_pack& regs)
		{
			CEntryExit* entry = (CEntryExit*)regs.esi;
			CEntryExit* exit = nullptr;
			if (loadedEnexAttachHashes.size() > 0 && entry) {
				exit = FindEnexAttachedByHash(entry);
			}
			if (exit == nullptr)
			{
				if ((CEntryExitManager::mp_poolEntryExits->m_byteMap[(uint16_t)regs.eax].IntValue() & 0x80) == 0) {
					exit = (CEntryExit*)((char*)CEntryExitManager::mp_poolEntryExits->m_pObjects + sizeOfEnexStruct * (uint16_t)regs.eax);
				}
				//exit = entry->m_pLink; //uses game default link, cause bugs in safe houses but fixes mods that adds single entry-exit
			}
			regs.eax = (uint32_t)exit;
		});

    }

	static void RunFixes()
	{
		// Automatically delete duplicated radar icons
		for (uint32_t i = 0; i < radarTracesTotal; i++)
		{
			if (radarTracesArray[i].m_bInUse)
			{
				for (uint32_t j = 0; j < radarTracesTotal; j++)
				{
					if (i != j && radarTracesArray[j].m_bInUse &&
						radarTracesArray[i].m_nBlipType == radarTracesArray[j].m_nBlipType &&
						(radarTracesArray[i].m_vecPos.x == radarTracesArray[j].m_vecPos.x) &&
						(radarTracesArray[i].m_vecPos.y == radarTracesArray[j].m_vecPos.y) &&
						(radarTracesArray[i].m_vecPos.z == radarTracesArray[j].m_vecPos.z))
					{
						lg << "Radar Icons: Fixed duplication. Type: " << (int)radarTracesArray[j].m_nBlipType <<
							" x: " << radarTracesArray[j].m_vecPos.x <<
							" y: " << radarTracesArray[j].m_vecPos.y <<
							" z: " << radarTracesArray[j].m_vecPos.z << "\n";
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
						if (IsException(carGeneratorArray[j]) == false)
						{
							lg << "Car Generators: Fixed duplication. Model: " << (int)carGeneratorArray[j].m_nModelId <<
								" x: " << carGeneratorArray[j].m_vecPosn.x / 8 <<
								" y: " << carGeneratorArray[j].m_vecPosn.y / 8 <<
								" z: " << carGeneratorArray[j].m_vecPosn.z / 8;
							lg << "\n";
							carGeneratorArray[j].SwitchOff();
							carGeneratorArray[j].m_bIsUsed = false;
							CTheCarGenerators::NumOfCarGenerators--;
						}
					}
				}
			}
		}

		lg << "Done fixes" << "\n";
		lg.flush();
	}

	static bool IsException(CCarGenerator carGenerator)
	{
		bool isException = false;
		switch (carGenerator.m_vecPosn.x)
		{
			case 17735:
				if (carGenerator.m_nModelId == 492 && carGenerator.m_vecPosn.y == -9283 && carGenerator.m_vecPosn.z == 197) isException = true;
				break;
			case 19265:
				if (carGenerator.m_nModelId == 545 && carGenerator.m_vecPosn.y == -13755 && carGenerator.m_vecPosn.z == 109) isException = true;
				break;
			case -14225:
				if (carGenerator.m_nModelId == 557 && carGenerator.m_vecPosn.y == 9656 && carGenerator.m_vecPosn.z == 201) isException = true;
				break;
			case -11197:
				if (carGenerator.m_nModelId == 599 && carGenerator.m_vecPosn.y == 21028 && carGenerator.m_vecPosn.z == 446) isException = true;
				break;
			case -3036:
				if (carGenerator.m_nModelId == 568 && carGenerator.m_vecPosn.y == -11544 && carGenerator.m_vecPosn.z == 205) isException = true;
				break;
			case -20576:
				if (carGenerator.m_nModelId == 442 && carGenerator.m_vecPosn.y == 9188 && carGenerator.m_vecPosn.z == 445) isException = true;
				break;
			case 16227:
				if (carGenerator.m_nModelId == 589 && carGenerator.m_vecPosn.y == 21848 && carGenerator.m_vecPosn.z == 84) isException = true;
				break;
		}
		return isException;
	}

} repairGTA;
