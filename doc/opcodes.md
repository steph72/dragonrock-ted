# DragonRock Opcodes

## 0x00 NOP / GOTO <01/02:destOpcodeIndex>
No operation

## 0x40 GOTO <01/02:destOpcodeIndex>
Continue execution at *destOpcodeIndex*

## 0x01 NSTAT <01:msgID>
Change current status message to *msgID*

## 0x21 NSTAT_O <01:msgID>
Like NSTAT, but clear status immediately after player moves on

## 0x02 DISP <01:msgID> <02:clrFlag>
Display *msgID*

## 0x22 DISP_S <01:msgID>
Display *msgID* in status area (always clears status area first)

## 0x03 WKEY <01:msgID> <02:clrFlag> <03:regNr>
Wait for keypress and display msgID. Stores pressed key in *regNr*

## 0x04 YESNO <01/02:trueOpcIdx> <03/04:falseOpcodeIdx>
Wait for 'y' or 'n' keypress
Register 0 -> true on 'yes', otherwise false.
if *y* and *trueOpcIdx*!=0 -> call subroutine at *trueOpcIdx*
if *n* and *falseOpcIdx*!=0 -> call subroutine at *falseOpcodeIdx*

## 0x44 YESNO_B <01/02:trueOpcIdx> <03/04:falseOpcodeIdx>
Like YESNO, but **branch** to trueOpcIdx instead of calling it

## 0x05 IFREG <01:regNr> <02:regValue> <03/04:trueOpcIdx> <05/06:falseOpcIdx>
If register *regNr* contains *regValue*, call subroutine at *trueOpcIdx*, else *falseOpcIdx*

## 0x45 IFREG_B <01:regNr> <02:regValue> <03/04:trueOpcIdx> 
Like IFREG, but **branch** to trueOpcIdx instead of calling it

## 0x06 IFPOS <01:itemId> <02:resultReg> <03/04:trueOpcIdx> <05/06:falseOpcIdx> 
If *itemId* is in current party's posession, perform *trueOpcIdx*, else *falseOpcIdx* 
Register #resultReg -> party member who is owner of itemID or 255 for not found

## 0x07 IADD <01:itemId> <02:charIdx> <03/04:successOpcIdx> <05/06:failureOpcIdx> <07:verboseFlag>
Add *itemId* to character *charIdx* inventory
If *charIdx*==0xff use first free character if posssible
If *verboseFlag* != 0, print '[characterName] took [itemName]' after successful completion
On success, performs <successOpcIdx>; otherwise <failureOpcIdx>
Register 0 -> true on success, otherwise false
Register 1 -> party member who took the item

## 0x08 ALTER <01:xpos> <02:ypos> <03:posOpcodeLabel> <04:dungeonItemID>
Alter map at coordinates *xpos*,*ypos* to opcode index pointed to by *posOpcodeLabel* and *dungeonItemID*. WARNING! DungeonItemID has to carry bits 9+10 of the final dungeon item ID address just like a "regular" map entry.
The map compiler automatically builds xpos,ypos from any defined (defc) label. 

## 0x09 REDRAW
Force redraw the dungeon display

## 0x0a ADDC <01:numCoinsL> <02:numCoinsH> <07:verboseFlag>
Give numCoins to the party
If *verboseFlag* != 0,  output 'coins taken' message

## 0x0b ADDE <01:numExpL> <02:numExpH> <07: verboseFlag>
Give numExp experience to party
If *verboseFlag* != 0,  output 'exp taken' message

## 0x0c SETREG <01:regNum> <02:regVal>
Set register *regNum* to value *regVal*

## 0x0d CLRENC
Clear encounter list

## 0x0e ADDENC <01:mID> <02:mLvl> <03:minCount> <04:maxCount> <05:row> 
Add <minCount>  to <maxCount> monsters with monster ID <mID> of level <mLvl> to encounter row <row>

## 0x0f GOENC <01/02:winOpcIdx> <03/04:loseOpcIdx>
Start encounter

## 0x10 ENTER_W <01:mapId> <02:xpos> <03:ypos>
Enter wilderness map *mapID* at coords *xpos*, *ypos* and switch to outdoor mode

## 0x30 ENTER_D <01:dungeonId> <02:xpos> <03:ypos>
Enter dungeon *dungeonId* at coords *xpos*, *ypos* and switch to dungeon mode

## 0x11 ENTER_C <01:cityId>
Enter city *cityId* and switch to city mode

## 0x51 RANDOM_B <01/02:randomChance> <03/04:opcIdx>
Jump to *opcIdx* if *randomChance* is greater than a random value between 0 and 999

# Outdoor and dungeon maps
Outdoor maps are registered by setting bit 7 in their id (i.e. "128+mapnum")
