#include "encounter.h"
#include "congui.h"
#include "globals.h"
#include "memory.h"
#include "spell.h"
#include "types.h"
#include "utils.h"

#include <c64.h>
#include <unistd.h>

// clang-format off
#pragma code-name(push, "OVERLAY3");
// clang-format on

static char *encounterActionNoun[]= {"Wait",  "Thrust", "Attack", "Slash",
                                     "Parry", "Cast",   "Shoot"};

static char *encounterActionVerb[]= {"waits",   "thrusts", "attacks", "slashes",
                                     "parries", "casts",   "shoots"};

void setupCombatScreen(void);
byte runPreCombat(void);

byte combatStarted;

// get monster count for given row
char getMonsterCountForRow(byte row) {
    byte res, i;
    res= 0;
    for (i= 0; i < MONSTER_SLOTS; ++i) {
        if (gMonsterRows[row][i] != NULL) {
            res++;
        }
    }
    return res;
}

// get monster name for given row
char *getMonsterNameForRow(byte row) {
    monster *res;
    byte count= getMonsterCountForRow(row);
    res= gMonsterRows[row][0];
    if (res) {
        if (count > 1) {
            return pluralNameForMonsterID(res->monsterDefID);
        } else {
            return nameForMonsterID(res->monsterDefID);
        }
    }
    return "---";
}

byte getMonsterCount() {
    byte i, j, res;
    res= 0;
    for (i= 0; i < MONSTER_ROWS; ++i) {
        for (j= 0; j < MONSTER_SLOTS; ++j) {
            if (gMonsterRows[i][j]) {
                ++res;
            }
        }
    }
    return res;
}

// remove a monster at a given row, column
void removeMonster(byte row, byte column) {
    byte i;
    monster *m;

    m= gMonsterRows[row][column];
    m->row= 255;
    m->column= 255;

    for (i= column; i < MONSTER_SLOTS - 1; ++i) {
        gMonsterRows[row][i]= gMonsterRows[row][i + 1];
        gMonsterRows[row][i]->column= i;
    }

    gMonsterRows[row][MONSTER_SLOTS]= NULL;
}

// remove a monster from combat
// (does NOT delete or free the monster, just removes it from active
// encounter)
void removeMonsterFromCombat(monster *aMonster) {
    removeMonster(aMonster->row, aMonster->column);
}

char *statusLineForRow(byte row) {
    sprintf(drbuf, "Rank %d - %d %s", row + 1, getMonsterCountForRow(row),
            getMonsterNameForRow(row));
    return drbuf;
}

void signalPreCombatResult(preCombatResult res) {
    char *results[]= {"The monsters greet you.",
                      "The monsters surrender.",
                      "The monsters take your money.",
                      "You don't get away.",
                      "You manage to flee",
                      "A fight begins!",
                      "No response"};

    setupCombatScreen();
    textcolor(COLOR_CYAN);
    puts(results[res]);
    sleep(1);
}

void showParty() {
    byte i;
    character *aChar;
    gotoxy(0, 0);
    textcolor(COLOR_GRAY2);
    cputs("# Name          Att HP      MP   Status");
    // show party
    for (i= 0; i < partyMemberCount(); ++i) {
        textcolor(COLOR_GREEN);
        aChar= party[i];
        gotoxy(0, 1 + i);
        printf("%d %s", i + 1, aChar->name);
        gotoxy(17, 1 + i);
        printf("%d", getNumberOfAttacks(aChar));
        gotoxy(19, 1 + i);
        sprintf(drbuf, "%d/%d", aChar->aHP, aChar->aMaxHP);
        if (strlen(drbuf) < 5) {
            cputc(' ');
        }
        cputs(drbuf);
        gotoxy(27, 1 + i);
        sprintf(drbuf, "%d/%d", aChar->aMP, aChar->aMaxMP);
        if (strlen(drbuf) < 5) {
            cputc(' ');
        }
        cputs(drbuf);
        gotoxy(33, 1 + i);
        if (aChar->status == down) {
            textcolor(COLOR_VIOLET);
            printf("down");
        } else if (aChar->status == dead) {
            textcolor(COLOR_RED);
            printf("dead");
        } else if (aChar->status == sleep) {
            textcolor(COLOR_LIGHTBLUE);
            printf("asleep");
        } else {
            textcolor(COLOR_GREEN);
            printf("ok");
        }
    }
}

void rollInitiative() {
    byte i, j;
    monster *aMonster;
    character *aChar;
    for (i= 0; i < MONSTER_ROWS; ++i) {
        for (j= 0; j < getMonsterCountForRow(i); ++j) {
            aMonster= gMonsterRows[i][j];
            aMonster->initiative= 1 + drand(32);
        }
    }
    for (i= 0; i < partyMemberCount(); ++i) {
        aChar= party[i];
        aChar->initiative=
            1 + drand(32) + bonusValueForAttribute(aChar->attributes[aDEX]);
    }
}

character *getRandomCharacter() {
    character *retChar;
    do {
        retChar= party[drand(partyMemberCount())];
    } while (retChar->status == dead || retChar->status == down);
    return retChar;
}

void updateMonsterStatus(monster *aMonster) {
    // TODO
    if (aMonster->hp <= 0) {
        aMonster->status= dead;
        printf("%s dies.\n", nameForMonster(aMonster));
    }
}

void updateCharacterStatus(character *aChar) {
    if (aChar->aHP == 0 ||
        (aChar->aHP < 0 &&
         aChar->aHP >= bonusValueForAttribute(aChar->attributes[aCON]))) {
        printf("\n%s goes down.\n", aChar->name);
        aChar->status= down;
        return;
    }

    if (aChar->aHP < 0) {
        printf("\n%s dies!\n", aChar->name);
        aChar->status= dead;
    }
}

static char *dtDefault= "";
static char *dtFrost= "cold ";
static char *dtFire= "fire ";

void monsterAttackSingleCharacter(monster *aMonster, character *aChar,
                                  byte attackTypeIndex) {

    byte hitRoll;
    byte isCritical;
    byte isCriticalFailure;
    unsigned int damage;
    signed char acHit;
    signed char destinationAC;
    monsterDef *def; 
    attackType type; 
    char *damageType;

    def = monsterDefForMonster(aMonster);
    type = def->aType[attackTypeIndex];
    damageType = dtDefault;

    if (type == at_fire) {
        damageType = dtFire;
    } else if (type == at_ice) {
        damageType = dtFrost;
    } 
    destinationAC= getArmorClassForCharacter(aChar);
    hitRoll= 1 + drand(20);
    isCritical= (hitRoll == 20);
    isCriticalFailure= (hitRoll == 1);
    hitRoll+= def->hitModifier[attackTypeIndex];
    acHit= 20 - hitRoll;
    printf("%s attacks %s\nand ", nameForMonsterID(aMonster->monsterDefID),
           aChar->name);
    // printf("(roll %d, AC %d, dest AC %d)\n", hitRoll,
    // acHit,destinationAC);

    damage= def->minDmg[attackTypeIndex] +
            drand(def->maxDmg[attackTypeIndex] - def->minDmg[attackTypeIndex]) +
            def->hitModifier[attackTypeIndex];

    if (acHit > destinationAC) {
        if (isCriticalFailure) {
            printf("critically misses,\ngetting hurt for %d points of "
                   "damage.",
                   damage);
            aMonster->hp-= damage;
            updateMonsterStatus(aMonster);
        } else {
            printf("misses.");
        }
    } else {
        if (isCritical) {
            damage*= 2;
            printf("critically hits for\n%d points of %sdamage.", damage, damageType);
        } else {
            printf("hits for %d points of %sdamage.", damage, damageType);
        }
        aChar->aHP-= damage;
        updateCharacterStatus(aChar);
    }
}

encResult doMonsterTurn(monster *aMonster) {
    character *aChar;
    byte i;

    byte numAttacks= getNumberOfMonsterAttacks(aMonster);
    monsterDef *def= monsterDefForMonster(aMonster);

    // TODO: charmed and sleep state
    if (aMonster->status != awake) {
        return encFight;
    }

    for (i= 0; i < numAttacks; ++i) {
        aChar= getRandomCharacter();
        monsterAttackSingleCharacter(aMonster, aChar, i);
        printf("\n\n");
        sleep(1);
    }
    return encFight;
}

encResult doCharacterTurn(character *aChar) {
    // printf("%x %s:%d (%d)\n", aChar, aChar->name, aChar->initiative,
    //       bonusValueForAttribute(aChar->attributes[aDEX]));
    return encFight;
}

encResult doFight() {
    signed char currentInitiative;
    monster *aMonster;
    character *aChar;
    encResult res;
    byte i, j;
    combatStarted = true;
    rollInitiative();
    while (1) {
        clrscr();
        for (currentInitiative= 64; currentInitiative > -64;
             --currentInitiative) {
            // check if monster's turn
            for (i= 0; i < MONSTER_ROWS; ++i) {
                for (j= 0; j < MONSTER_SLOTS; ++j) {
                    aMonster= gMonsterRows[i][j];
                    if (aMonster && aMonster->initiative == currentInitiative) {
                        res= doMonsterTurn(aMonster);
                        if (res != encFight) {
                            return res;
                        }
                    }
                }
            }
            // check if character's turn
            for (i= 0; i < partyMemberCount(); ++i) {
                aChar= party[i];
                if (aChar->initiative == currentInitiative) {
                    res= doCharacterTurn(aChar);
                    if (res != encFight) {
                        return res;
                    }
                }
            }
        }
        cg_getkey();
        res= runPreCombat();
        if (res != preCombatResultBeginFight) {
            signalPreCombatResult(res);
        }

        switch (res) {
        case preCombatResultFleeSuccess:
            return encFled;
            break;

        case preCombatResultMercy:
            return encMercy;
            break;

        case preCombatResultSurrender:
            return encSurrender;
            break;

        default:
            break;
        }
    }
}

encResult doEncounter() {
    preCombatResult res;
    combatStarted= false;
    res= runPreCombat();
    signalPreCombatResult(res);
    if (res == preCombatResultBeginFight || res == preCombatResultFleeFailure) {
        return doFight();
    } else if (res == preCombatResultFleeSuccess) {
        return encFled;
    }
    clearMonsters();
    return encWon;
}

// ------------------ screen config ---------------------

void setupCombatScreen(void) {
    bgcolor(0);
    textcolor(4);
    bordercolor(2);
    clrscr();
}

// ------------------ courage handling  ---------------------

unsigned int mc, pc;

unsigned int partyCourage() {
    byte i;
    unsigned int courage= 0;
    character *aChar;

    for (i= 0; i < partyMemberCount(); ++i) {
        aChar= party[i];
        courage+= (aChar->level) * 100;
        courage+= bonusValueForAttribute(aChar->attributes[aCHR]) *
                  25; // add/remove CHR bonus
    }

    return courage;
}

unsigned int monstersCourage() {
    byte i, j;
    unsigned int courage= 0;
    monster *aMonster;
    monsterDef *def;
    for (i= 0; i < MONSTER_ROWS; ++i) {
        for (j= 0; j < MONSTER_SLOTS; ++j) {
            aMonster= gMonsterRows[i][j];
            if (aMonster) {
                def= monsterDefForID(aMonster->monsterDefID);
                courage+= (aMonster->level) * 110;
                courage+= (def->courageModifier) * 25;
            }
        }
    }
    if (combatStarted) {
        courage+= 100;
    }
    return courage;
}

void updateCourage() {
    mc= monstersCourage() + drand(50 * getMonsterCount());
    pc= partyCourage();
    gotoxy(0, 22);
    printf("pCourage, mCourage: %d, %d ", pc, mc);
}

preCombatResult checkGreet() {
    if (combatStarted) {
        return preCombatResultNoResponse;
    }
    if (mc < pc) {
        return preCombatResultGreet;
    }
    return preCombatResultBeginFight;
}

preCombatResult checkThreaten() {
    if (mc < pc) {
        return preCombatResultSurrender;
    }
    return combatStarted ? preCombatResultNoResponse
                         : preCombatResultBeginFight;
}

preCombatResult checkBegForMercy() {
    if (abs(mc - pc) < 10) {
        return preCombatResultMercy;
    }
    return combatStarted ? preCombatResultNoResponse
                         : preCombatResultBeginFight;
}

preCombatResult preCombatResultForChoice(byte choice) {

    preCombatResult res;
    updateCourage();

    switch (choice) {
    case 'g':
        res = checkGreet();
        break;

    case 't': // threaten
        res = checkThreaten();
        break;

    case 'b': // beg for mercy
        res = checkBegForMercy();
        break;

    case 'f': // fight
        res = combatStarted ? preCombatResultNoResponse
                             : preCombatResultBeginFight;
        break;

    case 'r': // flee
        if (drand(100) > 50) {
            res= preCombatResultFleeSuccess;
        } else {
            res= preCombatResultFleeFailure;
        }
        break;

    default:
        res= preCombatResultNoResponse;
        break;
    }

    return res;
}

byte runPreCombat(void) {
    byte i;
    byte choice;

    setupCombatScreen();
    showParty();

    // show enemies
    textcolor(COLOR_ORANGE);
    gotoxy(0, 11);
    puts("You are facing:");
    textcolor(COLOR_LIGHTRED);
    for (i= 0; i < MONSTER_ROWS; i++) {
        gotoxy(0, 12 + i);
        if (getMonsterCountForRow(MONSTER_ROWS - i - 1)) {
            cputs(statusLineForRow(MONSTER_ROWS - i - 1));
        }
    }
    gotoxy(0, 20);
    textcolor(COLOR_GREEN);
    puts("g)reet  t)hreaten  b)eg for mercy");
    puts("f)ight  r)un away");
    textcolor(COLOR_ORANGE);
    do {
        gotoxy(0, 22);
        cputs("Your choice:");
        cursor(1);
        choice= cg_getkey();
        cursor(0);
    } while (strchr("gtbfr", choice) == NULL);

    return preCombatResultForChoice(choice);
}

// clang-format off
#pragma code-name(pop);
// clang-format on
