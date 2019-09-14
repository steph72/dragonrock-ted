#include "encounter.h"
#include <unistd.h>

byte iteratorRow= 0;
byte iteratorColumn= 0;

byte gCurrentSpriteCharacterIndex;
byte idxTable[255]; // sprite index cache
static char sfname[8];

unsigned int partyAuthorityLevel;
unsigned int monsterAuthorityLevel;

// 0x0a: ADDC / ADDE / ADDC_V / ADDE_V
byte performAddCoinsOpcode(opcode *anOpcode) {
    byte charIdx;
    byte opcodeID;
    int *coins;
    int numMembers;
    int coinsPerMember;

    opcodeID= anOpcode->id & 31;

    numMembers= partyMemberCount();
    coins= (int *)&(
        anOpcode->param1); // ...try to do something like that in Swift!
    coinsPerMember= *coins / numMembers;

    for (charIdx= 0; charIdx < PARTYSIZE; ++charIdx) {
        if (party[charIdx]) {
            if (opcodeID == 0x0a) {
                party[charIdx]->gold+= coinsPerMember;
            } else if (opcodeID == 0x0b) {
                party[charIdx]->xp+= coinsPerMember;
            }
        }
    }

    if (anOpcode->id & 128) {
        if (opcodeID == 0x0a) {
            printf("The party gets %d coins\n", coins);
        } else if (opcodeID == 0x0b) {
            printf("The party gets %d experience points\n", coins);
        }
    }

    return 0;
}

void giveCoins(unsigned int coins) {
    opcode fakeOpcode;
    fakeOpcode.id= 0x8a;
    fakeOpcode.param1= coins % 256;
    fakeOpcode.param2= coins / 256;
    performAddCoinsOpcode(&fakeOpcode);
}

void giveExperience(unsigned int exp) {
    opcode fakeOpcode;
    fakeOpcode.id= 0x8b;
    fakeOpcode.param1= exp % 256;
    fakeOpcode.param2= exp / 256;
    performAddCoinsOpcode(&fakeOpcode);
}

byte xposForMonster(byte numMonsters, byte mPos, byte mWidth) {
    byte width;
    width= 40 / numMonsters;
    return (width * mPos) + (width / 2) - (mWidth / 2);
}

void doMonsterTurn(byte row, byte column) {

    monster *theMonster;
    theMonster= gMonsterRow[row][column];
    printf("doing %s(%d at %d,%d)\n", theMonster->def->name,
           theMonster->initiative, row, column);
    // cgetc();
}

void doPartyTurn(byte idx) {
    character *theCharacter;
    theCharacter= party[idx];
    printf("doing %s(%d at %d)\n", theCharacter->name, theCharacter->initiative,
           idx);
    // cgetc();
}

void plotSprite(byte x, byte y, byte spriteID) {
    byte i, j;
    byte *screenPtr;
    byte charIdx;
    screenPtr= SCREEN + (x - 1 + (y * 40));
    charIdx= idxTable[spriteID] - 1;
    for (i= 0; i < 3; ++i) {
        for (j= 0; j < 3; ++j) {
            *(++screenPtr)= ++charIdx;
        }
        screenPtr+= 37;
    }
}

void plotMonster(byte row, byte idx) {
    byte x, y;

    x= xposForMonster(gNumMonsters[row], idx, 3);
    y= ((2 - row) * 4);

    plotSprite(x, y, gMonsterRow[row][idx]->def->spriteID);
}

void plotCharacter(byte idx) {
    byte x, y;

    x= xposForMonster(partyMemberCount(), idx, 3);
    y= 13;

    plotSprite(x, y, party[idx]->spriteID);
}

void loadSprite(byte id) {
    byte *addr;
    FILE *spritefile;

    sprintf(sfname, "spr%03d", id);
    // spritefile= fopen(sfname, "rb");
    spritefile= fopen("spr128", "rb");
    cputc('.');
    addr= (byte *)0xf000 + (gCurrentSpriteCharacterIndex * 8);
    // printf("\n%s -> %d @ $%x", sfname, gCurrentSpriteCharacterIndex, addr);
    if (spritefile) {
        fread(addr, 144, 1, spritefile);
        fclose(spritefile);
    } else {
        printf("!spritefile %s not found", spritefile);
    }
    idxTable[id]= gCurrentSpriteCharacterIndex;

    /*
        one sprite takes 18 characters (144 bytes),
        3x3 rows = 9 characters x 2 for each state,
        which gives us space for 14 sprites in one
        charset.

    */

    gCurrentSpriteCharacterIndex+= 18;
}

void loadSpriteIfNeeded(byte id) {

    if (idxTable[id] != 255) {
        return;
    }
    loadSprite(id);
}

void showFightOptionStatus(char *status) {
    cg_clearLower(7);
    gotoxy(0, 18);
    cputs(status);
    sleep(1);
}

encResult checkSurrender() {
    unsigned int tMonsterAuth;
    tMonsterAuth= monsterAuthorityLevel + rand() % 3;
    if (tMonsterAuth < partyAuthorityLevel) {
        showFightOptionStatus("The monsters surrender!");
        return encWon;
    }
    return encFight;
}

encResult checkMercy() {
    encResult res = encFight;
    if (monsterAuthorityLevel < partyAuthorityLevel) {
        if ((rand() % 10) > 7) {
            res = encMercy;
        }
    }
    if ((rand() % 10) > 3) {
        res = encMercy;
    }
    if (res==encMercy) {
        showFightOptionStatus("The monsters have mercy!");
    }
    return res;
}

byte iterateMonsters(monster **currentMonster, byte *row, byte *column) {

    *row= iteratorRow;
    *column= iteratorColumn;

    if (iteratorRow == MONSTER_ROWS) {
        iteratorRow= 0;
        iteratorColumn= 0;
        *currentMonster= NULL;
        return 0;
    }

    *currentMonster= gMonsterRow[iteratorRow][iteratorColumn];

    if (iteratorColumn == MONSTER_SLOTS - 1) {
        iteratorColumn= 0;
        ++iteratorRow;
    } else {
        ++iteratorColumn;
    }

    return 1;
}

encResult preEncounter(void) {

    static byte i, j;
    static byte totalMonsterCount;
    static byte livePartyMembersCount;
    static monster *aMonster;
    static char choice;
    character *aCharacter;

    monsterAuthorityLevel= 0;

    clrscr();
    textcolor(BCOLOR_RED | CATTR_LUMA3);
    chlinexy(0, 10, 40);
    chlinexy(0, 16, 40);
    textcolor(BCOLOR_WHITE | CATTR_LUMA6);
    showCurrentParty(false);
    gotoxy(0, 12);

    // display ranks and compute authority level while we're at it
    totalMonsterCount= 0;
    while (iterateMonsters(&aMonster, &i, &j)) {
        if (aMonster) {
            ++totalMonsterCount;
            monsterAuthorityLevel+= aMonster->level;
            monsterAuthorityLevel+= aMonster->def->courageModifier;
        }
    }

    for (i= 0; i < MONSTER_ROWS; ++i) {
        j= gNumMonsters[i];
        if (j) {
            aMonster= gMonsterRow[i][0];
            printf("Rank %d: %d %s(s)\n", i + 1, gNumMonsters[i],
                   aMonster->def->name);
        }
    }

    monsterAuthorityLevel/= totalMonsterCount;

    // calc party authority level
    partyAuthorityLevel= 0;
    livePartyMembersCount= 0;
    for (i= 0; i < partyMemberCount(); ++i) {
        aCharacter= party[i];
        if (aCharacter->status == alive) {
            ++livePartyMembersCount;
            partyAuthorityLevel+= aCharacter->level;
            partyAuthorityLevel+=
                bonusValueForAttribute(aCharacter->attributes[0]);
        }
    }
    partyAuthorityLevel/= livePartyMembersCount;

#ifdef DEBUG
    gotoxy(30, 17);
    printf("(P%d/M%d)", partyAuthorityLevel, monsterAuthorityLevel);
#endif
    gotoxy(0, 18);
    puts("1) Fight      2) Accept Surrender");
    puts("3) Greetings  4) Beg for mercy");
    puts("5) Flee\n");
    cputs(">");
    cursor(1);

    do {
        choice= cgetc();
    } while (choice < '1' || choice > '6');
    cursor(0);

    switch (choice) {
    case '1':
        return encFight; // just fight
        break;

    case '2':
        return checkSurrender();
        break;

    case '4':
        return checkMercy();
        break;

#ifdef DEBUG
    case '6':
        return encWon; // debug win
        break;
#endif

    default:
        break;
    }

    return encFight;
}

void prepareMonsters(void) {

    byte i, j;
    monster *aMonster;

    while (iterateMonsters(&aMonster, &i, &j)) {
        if (aMonster) {
            loadSpriteIfNeeded(aMonster->def->spriteID);
            aMonster->initiative= (byte)(rand() % 20);
        }
    }
}

void prepareCharacters(void) {
    byte i;
    for (i= 0; i < partyMemberCount(); ++i) {
        party[i]->initiative=
            (rand() % 20) + bonusValueForAttribute(party[i]->attributes[3]);
        loadSpriteIfNeeded(party[i]->spriteID);
    }
}

encResult encLoop(void) {

    byte c, i, j;
    monster *aMonster;
    encResult res;
    byte stopEncounter;

    bordercolor(BCOLOR_BLACK);
    bgcolor(BCOLOR_BLACK);
    textcolor(BCOLOR_WHITE | CATTR_LUMA6);

    clrscr();
    cputs("An encounter! ");

    gCurrentSpriteCharacterIndex= 0;
    memset(idxTable, 255, 255);
    prepareMonsters();
    prepareCharacters();
    stopEncounter= false;

    do {

        setSplitEnable(0);
        cg_emptyBuffer();
        res= preEncounter();

        if (res != encFight) {
            setSplitEnable(0);
            clrscr();
            cputs("Please wait...");
            return res;
        }

        setSplitEnable(1);
        cg_clear();

        gotoxy(0, 17);

        // initial plot of monsters & do monster initiative rolls

        while (iterateMonsters(&aMonster, &i, &j)) {
            if (aMonster) {
                plotMonster(i, j);
            }
        }

        // do party initiative rolls & plot

        for (j= 0; j < PARTYSIZE; ++j) {
            if (party[j]) {
                plotCharacter(j);
            }
        }

        // main encounter loop

        for (c= 20; c != 0; --c) {
            while (iterateMonsters(&aMonster, &i, &j)) {
                if (aMonster && aMonster->initiative == c) {
                    doMonsterTurn(i, j);
                }
            }
            for (j= 0; j < PARTYSIZE; ++j) {
                if (party[j] && party[j]->initiative == c) {
                    doPartyTurn(j);
                }
            }
        }

    } while (!stopEncounter);
    return encWon;
}

void takeMoney(void) {
    byte i;
    for (i= 0; i < partyMemberCount(); ++i) {
        party[i]->gold= 0;
    }
    puts("The monsters take all your money.\n");
}

void postKill(byte takeLoot) {

    unsigned int experience= 0;
    unsigned int coins= 0;
    byte newCoins;

    byte i, j;
    monster *aMonster;

    while (iterateMonsters(&aMonster, &i, &j)) {
        if (aMonster != NULL) {
            newCoins= 0;
            if (aMonster->hp <= 0) {
                experience+= aMonster->def->xpBaseValue;
                newCoins= aMonster->def->xpBaseValue / 10;
            }
            if (takeLoot) {
                if (!(aMonster->def->type & mt_animal)) {
                    if (newCoins == 0) {
                        newCoins= 1;
                    }
                    coins+= newCoins;
                }
            }
        }
    }

    if (coins > 0) {
        giveCoins(coins);
    }

    if (experience > 0) {
        giveExperience(experience);
    }
}

encResult doEncounter(void) {

    encResult res;

    res= encLoop();
    setSplitEnable(0);

    clrscr();

    if (res == encWon) {
        postKill(true);
    }

    if (res == encMercy) {
        takeMoney();
    }

    if (res == encFled) {
        postKill(false);
    }

    cputs("-- key --\n");
    cgetc();

    return res;
}