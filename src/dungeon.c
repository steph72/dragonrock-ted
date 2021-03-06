#include "dungeon.h"
#include "character.h"
#include "congui.h"
#include "debug.h"
#include "dungeonLoader.h"
#include "encounter.h"
#include "globals.h"
#include "memory.h"
#include "monster.h"

#include <c64.h>
//#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ------------------------- opcodes ------------------------- */
#define OPC_NOP 0x00     /* no operation                       */
#define OPC_NSTAT 0x01   /* new status line                    */
#define OPC_DISP 0x02    /* display text                       */
#define OPC_WKEY 0x03    /* waitkey                            */
#define OPC_YESNO 0x04   /* request y or n                     */
#define OPC_IFREG 0x05   /* if register                        */
#define OPC_IFPOS 0x06   /* if item in possession              */
#define OPC_IADD 0x07    /* add item to character's inventory  */
#define OPC_ALTER 0x08   /* alter map at coordinates           */
#define OPC_REDRAW 0x09  /* redraw screen                      */
#define OPC_ADDC 0x0a    /* add coins                          */
#define OPC_ADDE 0x0b    /* add experience                     */
#define OPC_SETREG 0x0c  /* set register                       */
#define OPC_CLRENC 0x0d  /* clear encounter                    */
#define OPC_ADDENC 0x0e  /* add monsters to encounter row      */
#define OPC_DOENC 0x0f   /* do encounter                       */
#define OPC_ENTER 0x10   /* enter dungeon or wilderness        */
#define OPC_ENTERC 0x11  /* enter city                         */
#define OPC_RANDOMB 0x12 /* random branch                      */
/* ----------------------------------------------------------  */

// clang-format off
#pragma code-name(push, "OVERLAY1");
// clang-format on

typedef struct _sign {
    byte characterCode;
    byte blocksViewFlag;
} sign;

sign signs[]= {

    /* -- dungeon signs -- */

    {12, 0}, //  0  empty space
    {7, 0},  //  1  diamond
    {15, 1}, //  2  vertical door
    {14, 1}, //  3  horizontal door
    {13, 1}, //  4  filled space

    /* -- outdoor signs -- */

    {20, 0},  //  5  grass
    {30, 0},  //  6  sand
    {24, 0},  //  7  stone path
    {22, 1},  //  8  trees1
    {22, 1},  //  9  trees2
    {26, 0},  // 10  water1
    {26, 0},  // 11  water2
    {18, 1},  // 12  hills
    {16, 1},  // 13  mountains
    {32, 0},  // 14  village
    {112, 0}, // 15  castle
    {112, 0}, // 16  inn
    {114, 0}, // 17  dungeon
    {28, 0}   // 18 bridge
};

byte isDungeonMode;

static dungeonDescriptor *desc= NULL;
unsigned int dungeonMapWidth;
byte lastFeelIndex;
byte registers[16];

#define R_PASS 15

byte currentX; // current coordinates inside map window
byte currentY;
int mposX; // current coords inside map
int mposY;
byte offsetX;
byte offsetY;

byte nstatOnceCounter;
byte quitDungeon;

const byte mapWindowSizeX= 15;
const byte mapWindowSizeY= 15;

const byte scrollMargin= 4;
const byte screenWidth= 40;
const byte screenX= 1;
const byte screenY= 1;

int encounterWonOpcIdx;
int encounterLostOpcIdx;

// prototypes

void fetchDungeonItemAtPos(byte x, byte y, dungeonItem *anItem);
void fetchOpcodeAtIndex(byte idx, opcode *anOpcode);
void fetchFeelForIndex(byte idx, char *aFeel);
void setDungeonItemAtPos(byte x, byte y, dungeonItem *anItem);

void displayFeel(byte idx);
void redrawAll(void);
void plotPlayer(byte x, byte y);
void setupScreen(void);

void performOpcodeAtIndex(int idx);
int performOpcode(opcode *anOpcode, int addr);

void clearStatus(void);

#ifdef DEBUG
byte singleStepMode;
#endif

// --------------------------------- opcodes ---------------------------------

// 0x00 0x40 NOP / GOTO
int performNopOrGotoOpcode(opcode *anOpcpode) {
    if (!(anOpcpode->id & 0x40)) {
        return 0;
    }
    return anOpcpode->param1 + (256 * anOpcpode->param2);
}

// 0x01: NSTAT / NSTAT_O
int performDisplayFeelOpcode(opcode *anOpcode) {
    byte feelIndex;

    feelIndex= anOpcode->param1;

    if (anOpcode->id & 32) {
        nstatOnceCounter= 1;
    }

    // make sure we display it only once
    if (feelIndex == lastFeelIndex) {
        return 0;
    }

    displayFeel(anOpcode->param1);
    lastFeelIndex= feelIndex;

    return 0;
}

// 0x02: DISP
int performDisplayTextOpcode(opcode *anOpcode) {

    byte feelIndex= anOpcode->param1;

    if (anOpcode->id & 0x20) {
        displayFeel(feelIndex);
        return 0;
    }

    if (anOpcode->param2 != 0) {
        cg_clrscr();
    }
    printFeelForIndex(anOpcode->param1);

    return 0;
}

// 0x03: WAITKEY
int performWaitkeyOpcode(opcode *anOpcode) {
    performDisplayTextOpcode(anOpcode);
    registers[anOpcode->param3]= cg_getkey();
    return 0;
}

// 0x04: YESNO / YESNO_B
int performYesNoOpcode(opcode *anOpcode) {
    byte inkey;
    int yesAddr;
    int noAddr;

    yesAddr= anOpcode->param1 + (256 * (anOpcode->param2));
    noAddr= anOpcode->param3 + (256 * (anOpcode->param4));

    cg_cursor(true);
    do {
        inkey= cg_getkey();
    } while (inkey != 'y' && inkey != 'n');
    cg_cursor(false);
    cg_printf("%c\n", inkey);
    if (inkey == 'y') {
        registers[0]= true;
        if (anOpcode->id & 0x40) {
            return yesAddr; // branch instead of call
        }
        performOpcodeAtIndex(yesAddr);
    } else if (inkey == 'n') {
        registers[0]= false;
        if (anOpcode->id & 0x40) { // branch?
            if (anOpcode->param2) {
                return noAddr; // jump there if we acutally got the parameter...
            } else {
                return 0; // jump to next opcode index
            }
        }
        // no branch: call noAddr
        performOpcodeAtIndex(noAddr);
    }
    return 0;
}

// 0x05: IFREG
int performIfregOpcode(opcode *anOpcode) {

    byte regNr;
    byte value;
    int yesAddr;
    int noAddr;

    yesAddr= anOpcode->param3 + (256 * (anOpcode->param4));
    noAddr= anOpcode->param5 + (256 * (anOpcode->param6));

    regNr= anOpcode->param1;
    value= anOpcode->param2;

    if (anOpcode->id & 0x40) {
        if (registers[regNr] == value) {
            return yesAddr;
        } else {
            return 0;
        }
    } else {
        if (registers[regNr] == value) {
            performOpcodeAtIndex(yesAddr);
        } else {
            performOpcodeAtIndex(noAddr);
        }
    }
    return 0;
}

// 0x06: IFPOS
int performIfposOpcode(opcode *anOpcode) {
    itemT anItemID;
    byte i;
    byte found;
    int yesAddr;
    int noAddr;

    yesAddr= anOpcode->param3 + (256 * (anOpcode->param4));
    noAddr= anOpcode->param5 + (256 * (anOpcode->param6));

    found= 0xff;
    anItemID= anOpcode->param1;

    for (i= 0; i < PARTYSIZE; ++i) {
        if (party[i]) {
            if (hasInventoryItem(party[i], anItemID)) {
                found= i;
                break;
            }
        }
    }

    registers[anOpcode->param2]= found;

    if (found != 0xff) {
        performOpcodeAtIndex(yesAddr);
    } else {
        performOpcodeAtIndex(noAddr);
    }
    return 0;
}

// 0x07: IADD
int performIAddOpcode(opcode *anOpcode) {
    byte charIdx;
    byte anItemID;
    byte found;
    int yesAddr;
    int noAddr;

    yesAddr= anOpcode->param3 + (256 * (anOpcode->param4));
    noAddr= anOpcode->param5 + (256 * (anOpcode->param6));

    anItemID= anOpcode->param1;
    charIdx= anOpcode->param2;
    found= false;

    if (charIdx == 0xff) {
        // choose first character with free inventory space
        for (charIdx= 0; charIdx < PARTYSIZE; ++charIdx) {
            if (party[charIdx]) {
                if (nextFreeInventorySlot(party[charIdx]) != 0xff) {
                    found= true;
                    break;
                }
            }
        }
        if (!found) {
            registers[0]= false; // failure flag
            performOpcodeAtIndex(noAddr);
            return 0;
        }
    }

    if (addInventoryItem(anItemID, party[charIdx])) {
        registers[0]= true;
        registers[1]= charIdx;
        if (anOpcode->param7) {
            cg_printf("%s takes %s\n", party[charIdx]->name,
                      nameOfInventoryItemWithID(anItemID));
        }
        performOpcodeAtIndex(yesAddr);
        return 0;
    }

    registers[0]= false;
    performOpcodeAtIndex(noAddr);
    return 0;
}

// 0x08: ALTER
int performAlterOpcode(opcode *anOpcode) {

    byte x, y;
    dungeonItem newDungeonItem;

    x= anOpcode->param1;
    y= anOpcode->param2;

    newDungeonItem.opcodeID= anOpcode->param3;
    newDungeonItem.mapItem= anOpcode->param4;

    setDungeonItemAtPos(x, y, &newDungeonItem);

    return 0;
}

// CAUTION: 0x0a has to be in the main segment
// because it is needed elsewhere as well..

// clang-format off
#pragma code-name(pop);
// clang-format on

// 0x0a: ADDC / ADDE / ADDC_V / ADDE_V
int performAddCoinsOpcode(opcode *anOpcode) {
    byte opcodeID;
    int *coins;

    opcodeID= anOpcode->id & 31;

    coins= (int *)&(
        anOpcode->param1); // ...try to do something like that in Swift!

    if (opcodeID == 0x0a) {
        gPartyGold+= *coins;
        if (anOpcode->param7) {
            cg_printf("The party gets %d coins\n", *coins);
        }

    } else {
        gPartyExperience+= *coins;
        if (anOpcode->param7) {
            cg_printf("The party gets %d experience\n", *coins);
        }
    }
    return 0;
}

// clang-format off
#pragma code-name(push, "OVERLAY1");
// clang-format on

// 0x0c: SETREG
int performSetregOpcode(opcode *anOpcode) {

    byte regNr;
    byte value;
    regNr= anOpcode->param1;
    value= anOpcode->param2;

    if (regNr > 16) {
        cg_printf("??invalid register nr %d... bailing out", regNr);
        while (1)
            ;
    }

    registers[regNr]= value;
    return 0;
}

// 0x0d: CLEARENC
int performClearencOpcode(void) {
    clearMonsters();
    return 0;
}

// 0x0e: ADDENC
int performAddencOpcode(opcode *anOpcode) {
    if (anOpcode->param1) {
        addNewMonster(anOpcode->param1, anOpcode->param2, anOpcode->param3,
                      anOpcode->param4, anOpcode->param5);
    }
    return 0;
}

// 0x0f DOENC
int performDoencOpcode(opcode *anOpcode) {

    // save result opcode indices for later on
    // when re-entering dungeon module
    cg_puts("doenc!");
    encounterWonOpcIdx= anOpcode->param1 + (256 * (anOpcode->param2));
    encounterLostOpcIdx= anOpcode->param3 + (256 * (anOpcode->param4));
    cg_puts("prepare");
    prepareForGameMode(gm_encounter);
    quitDungeon= true;
    /* cg_clearLower(5);
    cg_gotoxy(0, 19); */
    cg_clrscr();
    cg_puts("An encounter!");
    sleep(1);
    return 0;
}

// 0x10: ENTER_D / ENTER_W
int performEnterOpcode(opcode *anOpcode) {
    byte opcodeID;
    gameModeT newGameMode;

    opcodeID= anOpcode->id & 31;
    gCurrentDungeonIndex= anOpcode->param1;
    gStartXPos= anOpcode->param2;
    gStartYPos= anOpcode->param3;

    newGameMode= (anOpcode->id & 32) ? gm_dungeon : gm_outdoor;

    if (newGameMode == gm_outdoor) {
        gCurrentDungeonIndex|=
            128; // outdoor maps have bit 7 set in their index
    }

    prepareForGameMode(newGameMode);
    quitDungeon= true;
    return 0;
}

// 0x11: ENTER_C
int performEnterCityOpcode(opcode *anOpcode) {
    gCurrentCityIndex= anOpcode->param1;
    prepareForGameMode(gm_city);
    quitDungeon= true;
    return 0;
}

byte performRandomBranchOpcode(opcode *anOpcode) {
    int neededVal;
    int yesAddr;

    neededVal= anOpcode->param1 + (256 * (anOpcode->param2));
    yesAddr= anOpcode->param3 + (256 * (anOpcode->param4));

    if (neededVal > rand() % 1000) {
        return yesAddr;
    }

    return 0;
}

// ---------------------------------
// general opcode handling functions
// ---------------------------------

void performOpcodeAtIndex(int idx) {

    opcode anOpcode;
    int next;
    next= idx;

    do {
        fetchOpcodeAtIndex(next, &anOpcode);
        next= performOpcode(&anOpcode, next);
    } while (next);
}

int performOpcode(opcode *anOpcode, int currentPC) {

    byte xs, ys; // x,y save
    byte opcodeID;

    int newPC;
    int rOpcIdx; // returned opcode index from singular opcodes, used for
                 // branching

#ifdef DEBUG
    xs= cg_wherex();
    ys= cg_wherey();
    cg_gotoxy(0, 24);
    cg_printf("%04x:%02x%02x%02x%02x%02x%02x%02x>%02x", currentPC, anOpcode->id,
              anOpcode->param1, anOpcode->param2, anOpcode->param3,
              anOpcode->param4, anOpcode->param5, anOpcode->param6,
              anOpcode->param7); // DEBUG
    if (singleStepMode) {
        cg_gotoxy(28, 23);
        cg_puts("-- step --");
        cg_cgetc();
        cg_gotoxy(28, 0);
        cg_puts("          ");
    }
    cg_gotoxy(0, 23);
    cg_gotoxy(xs, ys);
#endif

    opcodeID= (anOpcode->id) & 31; // we have 5-bit opcodes from 0x00 - 0x1f

    switch (opcodeID) {

    case OPC_NOP:
        rOpcIdx= performNopOrGotoOpcode(anOpcode);
        break;

    case OPC_NSTAT:
        rOpcIdx= performDisplayFeelOpcode(anOpcode);
        break;

    case OPC_DISP:
        rOpcIdx= performDisplayTextOpcode(anOpcode);
        break;

    case OPC_WKEY:
        rOpcIdx= performWaitkeyOpcode(anOpcode);
        break;

    case OPC_YESNO:
        rOpcIdx= performYesNoOpcode(anOpcode);
        break;

    case OPC_IFREG:
        rOpcIdx= performIfregOpcode(anOpcode);
        break;

    case OPC_IFPOS:
        rOpcIdx= performIfposOpcode(anOpcode);
        break;

    case OPC_IADD:
        rOpcIdx= performIAddOpcode(anOpcode);
        break;

    case OPC_ALTER:
        rOpcIdx= performAlterOpcode(anOpcode);
        break;

    case OPC_REDRAW:
        redrawAll();
        rOpcIdx= 0;
        break;

    case OPC_ADDC:
        rOpcIdx= performAddCoinsOpcode(anOpcode);
        break;

    case OPC_ADDE:
        rOpcIdx= performAddCoinsOpcode(anOpcode);
        break;

    case OPC_SETREG:
        rOpcIdx= performSetregOpcode(anOpcode);
        break;

    case OPC_CLRENC:
        rOpcIdx= performClearencOpcode();
        break;

    case OPC_ADDENC:
        rOpcIdx= performAddencOpcode(anOpcode);
        break;

    case OPC_DOENC:
        rOpcIdx= performDoencOpcode(anOpcode);
        break;

    case OPC_ENTER:
        rOpcIdx= performEnterOpcode(anOpcode);
        break;

    case OPC_ENTERC:
        rOpcIdx= performEnterCityOpcode(anOpcode);
        break;

    case OPC_RANDOMB:
        rOpcIdx= performRandomBranchOpcode(anOpcode);
        break;

    default:
        rOpcIdx= 0;
        break;
    }

    // standard behaviour after processing an opcode: increase PC
    newPC= currentPC + 1;

    if (anOpcode->id & 0x80) { // end flag?
        newPC= 0;
    }

    if (anOpcode->id & 0x40) { // do we have a branch opcode?
        if (rOpcIdx != 0) {    // return index set?
            newPC= rOpcIdx;    // use returned index as next index
        }
    }

    // special case: address 0 always results in stopping opcode processing
    if (currentPC == 0) {
        newPC= 0;
    }

    return newPC;
}

void fetchDungeonItemAtPos(byte x, byte y, dungeonItem *anItem) {
    himemPtr adr;
    adr= (desc->dungeon) + ((x + (y * dungeonMapWidth)) * 2);
    anItem->mapItem= lpeek(adr);
    anItem->opcodeID= lpeek(adr + 1);
}

void setDungeonItemAtPos(byte x, byte y, dungeonItem *anItem) {
    himemPtr adr;
    adr= (desc->dungeon) + ((x + (y * dungeonMapWidth)) * 2);
    lpoke(adr, anItem->mapItem);
    lpoke(adr + 1, anItem->opcodeID);
}

void fetchOpcodeAtIndex(byte idx, opcode *anOpcode) {
    himemPtr adr;
    adr= desc->opcodesAdr + (idx * sizeof(opcode));
    lcopy(adr, (long)anOpcode, sizeof(opcode));
}

void redrawMap() { blitmap(offsetX, offsetY, screenX, screenY); }

void redrawAll() {
    setupScreen();
    redrawMap();
    showCurrentParty(0, 2, true);
    plotPlayer(currentX, currentY);
    if (lastFeelIndex != 255) {
        displayFeel(lastFeelIndex);
    }
}

void plotPlayer(byte x, byte y) {
    himemPtr screenPtr;
    himemPtr colorPtr;

    colorPtr= COLBASE + (screenWidth * screenY * 2) + (screenX * 2);
    screenPtr= SCREENBASE + (screenWidth * screenY * 2) + (screenX * 2);

    screenPtr+= x * 2 + (y * screenWidth * 2);
    colorPtr+= x * 2 + (y * screenWidth * 2);

    lpoke(screenPtr, 0x5f);
    lpoke(screenPtr + 1, 0);
    lpoke(colorPtr + 1, COLOR_WHITE);
}

void clearStatus(void) {
    cg_setwin(1, 17, 37, 6);
    cg_clrscr();
    cg_setwin(0, 0, 40, 24);
}

void displayFeel(byte feelID) {
    cg_setwin(1, 17, 37, 6);
    cg_clrscr();
    cg_textcolor(COLOR_YELLOW);
    printFeelForIndex(feelID);
    cg_textcolor(COLOR_GRAY2);
    cg_setwin(0, 0, 40, 24);
}

// moves origin and redraws map if player is in scroll area
void ensureSaneOffsetAndRedrawMap() {

    if (currentX > (mapWindowSizeX - scrollMargin - 1)) {
        if (offsetX + mapWindowSizeX < dungeonMapWidth) {
            ++offsetX;
            --currentX;
            redrawMap();
        }
    }

    if (currentX < scrollMargin + 1) {
        if (offsetX > 0) {
            --offsetX;
            ++currentX;
            redrawMap();
        }
    }

    if (currentY > mapWindowSizeY - scrollMargin - 1) {
        if (offsetY + mapWindowSizeY < desc->dungeonMapHeight) {
            ++offsetY;
            --currentY;
            redrawMap();
        }
    }

    if (currentY < scrollMargin + 1) {
        if (offsetY > 0) {
            --offsetY;
            ++currentY;
            redrawMap();
        }
    }
}

/*
breseham's line drawing algorithm for
field-of-view calculation
*/

void look_bh(int x0, int y0, int x1, int y1) {

    int dx, dy;
    int x, y;
    int n;
    int x_inc, y_inc;
    int error;

    dungeonItem anItem;
    byte rawMapItem;
    byte blocksView;

    dx= abs(x1 - x0);
    dy= abs(y1 - y0);
    x= x0;
    y= y0;
    n= 1 + dx + dy;
    x_inc= (x1 > x0) ? 1 : -1;
    y_inc= (y1 > y0) ? 1 : -1;
    error= dx - dy;

    dx*= 2;
    dy*= 2;

    for (; n > 0; --n) {

        fetchDungeonItemAtPos(x, y, &anItem);
        rawMapItem= anItem.mapItem & 31;
        blocksView= signs[rawMapItem].blocksViewFlag;

        if (x < 0 || y < 0 || x > desc->dungeonMapWidth - 1 ||
            y > desc->dungeonMapHeight - 1) {
            continue;
        }

        lpoke(seenMap + (x + (dungeonMapWidth * y)), anItem.mapItem);

        if (blocksView) {
            if (x != x0 || y != y0)
                return;
        }

        if (error > 0) {
            x+= x_inc;
            error-= dy;
        } else {
            y+= y_inc;
            error+= dx;
        }
    }
}

#define L_DISTANCE 4

// perform bresenham for 4 edges of f-o-v
void look(int x, int y) {
    signed char xi1, xi2, yi1, yi2;
    signed char xdiff, ydiff;
    dungeonItem anItem;

    for (xdiff= -1; xdiff <= 1; xdiff++) {
        for (ydiff= -1; ydiff <= 1; ydiff++) {
            mposX= x + xdiff;
            mposY= y + ydiff;
            if (mposX >= 0 && mposY >= 0 && mposX < dungeonMapWidth &&
                mposY < desc->dungeonMapHeight) {
                fetchDungeonItemAtPos(mposX, mposY, &anItem);
                if (xdiff | ydiff) {
                    lpoke(seenMap + (mposX + (dungeonMapWidth * mposY)),
                          anItem.mapItem);
                }
            }
        }
    }

    yi1= y - L_DISTANCE;
    if (yi1 < 0)
        yi1= 0;

    yi2= y + L_DISTANCE;
    if (yi2 > desc->dungeonMapHeight - 1)
        yi2= desc->dungeonMapHeight - 1;

    for (xi1= x - L_DISTANCE; xi1 <= x + L_DISTANCE; ++xi1) {
        look_bh(x, y, xi1, yi1);
        look_bh(x, y, xi1, yi2);
    }

    xi1= x - L_DISTANCE;
    if (xi1 < 0)
        xi1= 0;

    xi2= x + L_DISTANCE;
    if (xi2 > dungeonMapWidth - 1)
        xi2= dungeonMapWidth - 1;

    for (yi1= y - L_DISTANCE; yi1 <= y + L_DISTANCE; ++yi1) {
        look_bh(x, y, xi1, yi1);
        look_bh(x, y, xi2, yi1);
    }

    redrawMap();
}

unsigned int opcodeIndexForDungeonItem(dungeonItem *anItem) {
    unsigned int idx;
    unsigned int upperBytes;
    upperBytes= (anItem->mapItem & 192) << 2;
    idx= (anItem->opcodeID) | upperBytes;
    return idx;
}

void runDaemons(byte x, byte y) {
    byte i;
    daemonEntry currentDaemon;

    if (!desc->numDaemons)
        return;

    for (i= 0; i < desc->numDaemons; ++i) {
        currentDaemon= desc->daemonTbl[i];
        if (x >= currentDaemon.x1 && y >= currentDaemon.y1 &&
            x <= currentDaemon.x2 && y <= currentDaemon.y2) {
            performOpcodeAtIndex(currentDaemon.opcodeIndex);
        }
    }
}

void dungeonLoop() {

    byte xs, ys;     // save x,y for debugging
    byte oldX, oldY; // save x,y for impassable
    signed char fov;

    byte cmd;
    byte performedImpassableOpcode;

    dungeonItem dItem;
    dungeonItem currentItem;

    byte idx;

    fov= isDungeonMode ? 1 : 2;
    performedImpassableOpcode= false;
    redrawAll();

    /**************************************************************
     *
     * dloop starts here
     *
     **************************************************************/

    while (!quitDungeon) {

        ensureSaneOffsetAndRedrawMap();

        // establish fov and fill seenSpaces
        // (would have slowed down the plus/4 to a crawl, but is a breeze
        // on the mega65...)

        look(currentX + offsetX, currentY + offsetY);
        plotPlayer(currentX, currentY);

        // clear status area if nstat_o
        if (nstatOnceCounter) {
            nstatOnceCounter--;
            if (nstatOnceCounter == 0) {
                clearStatus();
                lastFeelIndex= 255;
            }
        }

        if (gEncounterResult !=
            encUndef) { // coming from an encounter? --> handle result first

            if ((gEncounterResult == encWon ||
                 gEncounterResult == encSurrender ||
                 gEncounterResult == encGreet) &&
                encounterWonOpcIdx) {

                performOpcodeAtIndex(encounterWonOpcIdx);
                gEncounterResult= encUndef;
                encounterWonOpcIdx= 0;
            }

            else if ((gEncounterResult == encFled ||
                      gEncounterResult == encMercy) &&
                     encounterLostOpcIdx) {

                performOpcodeAtIndex(encounterLostOpcIdx);
                gEncounterResult= encUndef;
                encounterWonOpcIdx= 0;
            } else {

                gEncounterResult=
                    encUndef; // reset global encounter result for next time...
            }
        }

        // alredy performed an opcode?
        if (!performedImpassableOpcode) {
            // no? run daemons if needed...
            runDaemons(currentX + offsetX, currentY + offsetY);

            // get what's under the player...
            fetchDungeonItemAtPos(currentX + offsetX, currentY + offsetY,
                                  &currentItem);
            // ...and perform it
            idx= opcodeIndexForDungeonItem(&currentItem);
            performOpcodeAtIndex(idx);
        }

        performedImpassableOpcode= false;

        oldX= currentX;
        oldY= currentY;

        cmd= 0;

        if (!quitDungeon) {

            cmd= cg_getkey();

            if (cmd >= '1' && cmd <= '6' && cmd != 'l') {
                inspectCharacter(cmd - '1');
                redrawAll();
            }
        }

        switch (cmd) {

        case 'l':
            look(currentX + offsetX, currentY + offsetY);
            break;

        case 29: // cursor right
            currentX++;
            break;

        case 157: // cursor left
            currentX--;
            break;

        case 145: // cursor up
            currentY--;
            break;

        case 17: // cursor down
            currentY++;
            break;

#ifdef DEBUG
        case 'q':
            quitDungeon= true;
            break;

        case 's':
            singleStepMode= !singleStepMode;
            cg_gotoxy(0, 0);
            cg_printf("single step mode %d   ", singleStepMode);
            break;
#endif

        default:
            break;
        }

        mposX= currentX + offsetX;
        mposY= currentY + offsetY;

        // can we move here at all?
        if (mposX < 0 || mposY < 0 || mposX >= dungeonMapWidth ||
            mposY >= desc->dungeonMapHeight) {
            // nope, reset position
            currentX= oldX;
            currentY= oldY;
        } else {
            // what is here?
            fetchDungeonItemAtPos(mposX, mposY, &dItem);
            // dItem= dungeonItemAtPos(mposX, mposY);

#ifdef DEBUG
            xs= cg_wherex();
            ys= cg_wherey();
            cg_gotoxy(27, 24);
            cg_printf("%2d,%2d: %02x %02x", mposX, mposY, dItem.opcodeID,
                      dItem.mapItem);
            cg_gotoxy(xs, ys);
#endif

            if (dItem.mapItem & 32) { // check impassable flag
                // can't go there: reset pass register...
                registers[R_PASS]= 255;
                // ...perform opcode...
                idx= opcodeIndexForDungeonItem(&dItem);
                performOpcodeAtIndex(idx);
                performedImpassableOpcode= true;
                // ...and check if 'pass' register has become valid...
                if (registers[R_PASS] == 255) {
                    // ...you can not pass
                    currentX= oldX;
                    currentY= oldY;
                }
            }
        }

    } // of while (!quitDungeon);
}

void printFeelForIndex(byte idx) {
    himemPtr adr;
    char current;
    adr= desc->feelTbl[idx];
    do {
        current= lpeek(adr++);
        cg_putc(current);
    } while (current);
}

void setupScreen() {
    cg_go16bit(0, 0);
    cg_bgcolor(COLOR_BLACK);
    cg_bordercolor(COLOR_BLACK);
    cg_textcolor(COLOR_GRAY2);
    cg_clrscr();
    cg_borders(true);
}

void unloadDungeon(void) {

    if (desc != NULL) {
        if (desc->feelTbl)
            free(desc->feelTbl);
        if (desc->daemonTbl)
            free(desc->daemonTbl);
        free(desc);
        desc= NULL;
    }

    gLoadedDungeonIndex= 255;
}

void initLoadedDungeon(void) {

    byte correctY= 0;

    encounterLostOpcIdx= 0;
    encounterWonOpcIdx= 0;

    gEncounterResult= encUndef;

    dungeonMapWidth= desc->dungeonMapWidth;

    gLoadedDungeonIndex= gCurrentDungeonIndex;

    lastFeelIndex= 255;

    // set start coordinates from global vars
    currentX= gStartXPos;
    currentY= gStartYPos;

    offsetX= 0;
    offsetY= 0;
    mposX= 0;
    mposY= 0;

    offsetX= currentX - (mapWindowSizeX / 2);
    currentX= mapWindowSizeX / 2;

    if (currentY > 2) {
        offsetY= currentY - (mapWindowSizeY / 2);
        currentY= mapWindowSizeY / 2;
    }

    // move view up if we're at the bottom
    while ((offsetY + currentY + mapWindowSizeY / 2) >=
           desc->dungeonMapHeight) {
        offsetY--;
        correctY++;
    }

    currentY+= correctY;
}

void loadNewDungeon(void) {

    char *mfile;
    char *dungeonFile= "map%03d";
    char *outdoorFile= "out%03d";

    mfile= gCurrentGameMode == gm_dungeon ? dungeonFile : outdoorFile;

    unloadDungeon();
    sprintf(drbuf, mfile, gCurrentDungeonIndex & 127);

    desc= loadMap(drbuf);

    if (!desc) {
        cg_puts("could not load dungeon ");
        cg_puts(mfile);
        while (1)
            ;
    }

    initLoadedDungeon();
}

void enterDungeonMode(byte reInitMap) {
#ifdef DEBUG
    if (gCurrentGameMode == gm_dungeon) {
        cg_puts("entering dungeon mode");
    } else if (gCurrentGameMode == gm_outdoor) {
        cg_puts("entering outdoor mode");
    } else {
        cg_puts("??unknown game mode in dungeon!");
        while (1)
            ;
    }
    singleStepMode= false;
#endif
    isDungeonMode= gCurrentGameMode == gm_dungeon;

    if (gLoadedDungeonIndex != gCurrentDungeonIndex) {
        loadNewDungeon();
    } else {
        if (reInitMap) {
            initLoadedDungeon();
        }
    }

    quitDungeon= false;
    nstatOnceCounter= 0;
    cg_resetPalette();

    dungeonLoop();
}

void blitmap(byte mapX, byte mapY, byte posX, byte posY) {

    himemPtr screenPtr;
    himemPtr colorPtr;
    word charIdx;
    himemPtr seenMapPtr;
    himemPtr bufPtr;
    unsigned int offset;
    byte mapItem;
    byte xs, ys;

    byte xpos;

    byte screenStride;
    byte mapStride;
    byte modifier;

    screenStride= (screenWidth - mapWindowSizeX) * 2;
    mapStride= dungeonMapWidth;

    colorPtr= COLBASE + (screenWidth * posY * 2) + posX * 2;
    screenPtr= SCREENBASE + (screenWidth * posY * 2) + posX * 2;
    seenMapPtr= seenMap + (mapY * dungeonMapWidth) + mapX - 1;

    offset= 0;

    for (ys= 0; ys < mapWindowSizeY; ++ys) {
        bufPtr= seenMapPtr + offset;
        for (xs= 0; xs < mapWindowSizeX; ++xs, screenPtr+= 2, colorPtr+= 2) {
            xpos= mapX + xs;
            mapItem= lpeek(++bufPtr);
            // mapItem= *(++bufPtr);
            modifier= 0;
            if (mapItem != 255) {
                mapItem&= 31;
                if (mapItem >= 5) {
                    modifier= xpos % 2;
                }
                // lpoke(colorPtr + 1, signs[mapItem].colour);
                charIdx= (EXTCHARBASE / 64) + signs[mapItem].characterCode +
                         modifier;
                lpoke(screenPtr, charIdx % 256);
                lpoke(screenPtr + 1, charIdx / 256);
                // lpoke(screenPtr, signs[mapItem].characterCode + modifier);
            } else {
                lpoke(screenPtr, isDungeonMode ? 160 : 32);
                lpoke(screenPtr + 1, 0);
                lpoke(colorPtr + 1, COLOR_BLACK);
                // lpoke(colorPtr + 1, isDungeonMode ? COLOR_GRAY1 :
                // COLOR_BLACK);
            }
        }
        screenPtr+= screenStride;
        colorPtr+= screenStride;
        offset+= desc->dungeonMapWidth;
    }
}

// clang-format off
#pragma code-name(pop);
// clang-format on
