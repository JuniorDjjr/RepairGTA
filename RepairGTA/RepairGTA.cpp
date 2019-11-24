#include "plugin.h"
#include "CRadar.h"
#include "TestCheat.h"

using namespace plugin;
using namespace std;

class RepairGTA {
public:
    RepairGTA() {

		// Mod stuff
		static fstream lg;
		lg.open("RepairGTA.log", fstream::out | fstream::trunc);

		// Radar icons
		static bool fixRadarIcons = false;
		static tRadarTrace *radarTracesArray;
		static unsigned int radarTracesTotal = 0;

		///////////////////////////////////////////////

		Events::initScriptsEvent += [] {
			radarTracesArray = (tRadarTrace*)(injector::ReadMemory<uintptr_t>(0x582829 + 4, true) - 0x14);
			radarTracesTotal = injector::ReadMemory<unsigned int>(0x58384C + 2, true);
			fixRadarIcons = true;
		};

		Events::processScriptsEvent.after += [] {

			// Manually delete near blips
			if (TestCheat("DELBLIP"))
			{
				CVector2D playerPos = FindPlayerPed(-1)->GetPosition();
				CVector2D blipPos;
				for (unsigned int i = 0; i < radarTracesTotal; i++) {
					if (radarTracesArray[i].m_bTrackingBlip && DistanceBetweenPoints(playerPos, radarTracesArray[i].m_vPosition) < 10.0f)
					{
						lg << "Radar Icons: Manually deleted, with type: " << (int)radarTracesArray[i].m_nBlipType <<
							" x: " << radarTracesArray[i].m_vPosition.x <<
							" y: " << radarTracesArray[i].m_vPosition.y <<
							" z: " << radarTracesArray[i].m_vPosition.z << "\n";
						CRadar::ClearActualBlip(i);
					}
				}
				lg.flush();
			}

			// Automatically delete duplicates
			if (fixRadarIcons) {

				for (unsigned int i = 0; i < radarTracesTotal; i++) {
					if (radarTracesArray[i].m_bTrackingBlip)
					{
						for (unsigned int j = 0; j < radarTracesTotal; j++) {
							if (i != j && radarTracesArray[j].m_bTrackingBlip &&
								radarTracesArray[i].m_nBlipType == radarTracesArray[j].m_nBlipType &&
								(radarTracesArray[i].m_vPosition.x == radarTracesArray[j].m_vPosition.x) &&
								(radarTracesArray[i].m_vPosition.y == radarTracesArray[j].m_vPosition.y) &&
								(radarTracesArray[i].m_vPosition.z == radarTracesArray[j].m_vPosition.z))
							{
								lg << "Radar Icons: Fixed duplication, with type: " << (int)radarTracesArray[i].m_nBlipType <<
									" x: " << radarTracesArray[i].m_vPosition.x <<
									" y: " << radarTracesArray[i].m_vPosition.y <<
									" z: " << radarTracesArray[i].m_vPosition.z << "\n";
								CRadar::ClearActualBlip(j);
							}
						}
					}
				}
				lg.flush();
				fixRadarIcons = false;
			}

		};
    }
} repairGTA;
