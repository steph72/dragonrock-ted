#include "config.h"
#include "types.h"

/*  ----
    main
    ---- */

extern char *drbuf;  // general purpose buffer, $40 bytes at $ff40
extern char *drbuf2; // general purpose buffer, $40 bytes at $ff80

/*  ----------
    dispatcher
    ---------- */

extern gameModeT gCurrentGameMode;  // current game mode

extern byte gCurrentDungeonIndex;   // current dungeon (==dungeon to enter on mode change)   
extern byte gLoadedDungeonIndex;    // currently loaded dungeon
extern byte gCurrentCityIndex;      // current city index
extern encResult gEncounterResult;  // result of last encounter

/*  ---------------
    character/party
    --------------- */

extern character *party[PARTYSIZE]; // current adventuring party
extern long int gPartyGold;         // gold to be distributed when entering city
extern long int gPartyExperience;   // xp to be distributed when entering city

/*  -------
    dungeon
    ------- */

extern byte *seenMap; // map of seen spaces in dungeon