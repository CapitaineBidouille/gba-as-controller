/* 
 * Copyright (c) 2016-2021, Extrems' Corner.org
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <gba_console.h>
#include <gba_dma.h>
#include <gba_input.h>
#include <gba_interrupt.h>
#include <gba_sio.h>
#include <gba_timers.h>
#include <gba_video.h>
#include "bios.h"

#define struct struct __attribute__((packed, scalar_storage_order("big-endian")))

#define ROM           ((int16_t *)0x08000000)
#define ROM_GPIODATA *((int16_t *)0x080000C4)
#define ROM_GPIODIR  *((int16_t *)0x080000C6)
#define ROM_GPIOCNT  *((int16_t *)0x080000C8)
#define GPIO_IRQ	0x0100	//! Interrupt on SI.

enum {
	ID_GBAKEY_A = 0,
	ID_GBAKEY_B = 1,
	ID_GBAKEY_START = 2,
	ID_GBAKEY_SELECT = 3,
	ID_GBAKEY_L = 4,
	ID_GBAKEY_R = 5,
	ID_GBAKEY_UP = 6,
	ID_GBAKEY_DOWN = 7,
	ID_GBAKEY_LEFT = 8,
	ID_GBAKEY_RIGHT = 9,
};

enum {
	ID_GCPAD_A = 0,
	ID_GCPAD_B = 1,
	ID_GCPAD_X = 2,
	ID_GCPAD_Y = 3,
	ID_GCPAD_START = 4,
	ID_GCPAD_Z = 5,
	ID_GCPAD_L = 6,
	ID_GCPAD_R = 7,
	ID_GCPAD_UP = 8,
	ID_GCPAD_DOWN = 9,
	ID_GCPAD_LEFT = 10,
	ID_GCPAD_RIGHT = 11,
};

static char aGameProfilesNames[7][30] = {
	"Custom profile",
	"Default",
	"Super Smash Ultimate",
	"Mario Kart Double Dash",
	"Mario Kart 8 Deluxe",
	"New Super Mario Bros",
	"Mario Kart Wii"
};

static int aDefaultProfileConfig[6] = {ID_GCPAD_A, ID_GCPAD_B, ID_GCPAD_START, ID_GCPAD_Z, ID_GCPAD_L, ID_GCPAD_R};

enum {
	CMD_ID = 0x00,
	CMD_STATUS = 0x40,
	CMD_ORIGIN,
	CMD_RECALIBRATE,
	CMD_STATUS_LONG,
	CMD_RESET = 0xFF
};

enum {
	MOTOR_STOP = 0,
	MOTOR_RUMBLE,
	MOTOR_STOP_HARD
};

struct buttons {
	uint16_t            : 1;
	uint16_t unknown    : 1;
	uint16_t get_origin : 1;
	uint16_t start      : 1;
	uint16_t y          : 1;
	uint16_t x          : 1;
	uint16_t b          : 1;
	uint16_t a          : 1;
	uint16_t use_origin : 1;
	uint16_t l          : 1;
	uint16_t r          : 1;
	uint16_t z          : 1;
	uint16_t up         : 1;
	uint16_t down       : 1;
	uint16_t right      : 1;
	uint16_t left       : 1;
};

static struct {
	uint16_t type;

	struct {
		uint8_t            : 1;
		uint8_t unknown    : 1;
		uint8_t get_origin : 1;
		uint8_t motor      : 2;
		uint8_t mode       : 3;
	} status;
} id;

static struct {
	struct buttons buttons;
	struct { uint8_t x, y; } stick;
	union {
		struct {
			struct { uint8_t x : 8, y : 8; } substick;
			struct { uint8_t l : 4, r : 4; } trigger;
			struct { uint8_t a : 4, b : 4; } button;
		} mode0;

		struct {
			struct { uint8_t x : 4, y : 4; } substick;
			struct { uint8_t l : 8, r : 8; } trigger;
			struct { uint8_t a : 4, b : 4; } button;
		} mode1;

		struct {
			struct { uint8_t x : 4, y : 4; } substick;
			struct { uint8_t l : 4, r : 4; } trigger;
			struct { uint8_t a : 8, b : 8; } button;
		} mode2;

		struct {
			struct { uint8_t x, y; } substick;
			struct { uint8_t l, r; } trigger;
		} mode3;

		struct {
			struct { uint8_t x, y; } substick;
			struct { uint8_t a, b; } button;
		} mode4;
	};
} status;

static struct {
	struct buttons buttons;
	struct { uint8_t x, y; } stick;
	struct { uint8_t x, y; } substick;
	struct { uint8_t l, r; } trigger;
	struct { uint8_t a, b; } button;
} origin = {
	.buttons  = { .use_origin = 1 },
	.stick    = { 128, 128 },
	.substick = { 128, 128 },
};

static uint8_t buffer[128];

static enum {
	RUMBLE_NONE = 0,
	RUMBLE_GBA,
	RUMBLE_NDS,
	RUMBLE_NDS_SLIDE,
	RUMBLE_EZFLASH_OMEGA_DE,
} rumble;

static bool has_motor(void)
{
	switch (ROM[0x59]) {
		case 0x59:
			switch (ROM[0xFFFFFF]) {
				case ~0x0002:
					rumble = RUMBLE_NDS;
					return true;
				case ~0x0101:
					rumble = RUMBLE_NDS_SLIDE;
					return true;
			}
			break;
		case 0x96:
			switch (ROM[0x56] & 0xFF) {
				case 'R':
				case 'V':
					rumble = RUMBLE_GBA;
					return true;
				case 'G':
					rumble = RUMBLE_EZFLASH_OMEGA_DE;
					return true;
			}
			break;
	}
	rumble = RUMBLE_NONE;
	return false;
}

static void set_motor(bool enable)
{
	switch (rumble) {
		case RUMBLE_NONE:
			break;
		case RUMBLE_GBA:
			ROM_GPIODIR  =      1 << 3;
			ROM_GPIODATA = enable << 3;
			break;
		case RUMBLE_NDS:
			if (enable)
				DMA3COPY(SRAM, SRAM, DMA_VBLANK | DMA_REPEAT | 1)
			else
				REG_DMA3CNT &= ~DMA_REPEAT;
			break;
		case RUMBLE_NDS_SLIDE:
			*ROM = enable << 8;
			break;
			
		case RUMBLE_EZFLASH_OMEGA_DE:
			if(enable) {
				ROM_GPIODATA = ROM_GPIODATA | 8;
			} else {
				ROM_GPIODATA = ROM_GPIODATA & ~8;
			}
			break;
	}
}

static char aGbaKeys[10][7] = {
	"A",
	"B",
	"START",
	"SELECT",
	"L",
	"R",
	"UP",
	"DOWN",
	"LEFT",
	"RIGHT"
};

static char aGcPadButtons[12][6] = {
	"A",
	"B",
	"X",
	"Y",
	"START",
	"Z",
	"L",
	"R",
	"UP",
	"DOWN",
	"LEFT",
	"RIGHT",
};

void SISetResponse(const void *buf, unsigned bits);
int SIGetCommand(void *buf, unsigned bits);

void consoleSetup(int phase) {
	consoleInit(0, 4, 0, NULL, 0, 15);
	if (phase == 1) {
		// Black background
		BG_COLORS[0] = RGB8(0, 0, 0);
	} else if (phase == 2) {
		// Indigo background (same color as a gc controller)
		BG_COLORS[0] = RGB8(56, 67, 141);
	}
	BG_COLORS[241] = RGB5(31, 31, 31); // Text color (white)
	SetMode(MODE_0 | BG0_ON);
}

static bool isGameProfileValid(int* aGameProfileConfig) {
	bool valid = true;
	int nGbaKey = 0;
	while (nGbaKey < 6 && valid) {
		int nRelatedGcPadButton = aGameProfileConfig[nGbaKey];
		int i = nGbaKey + 1;
		while (i < 6 && valid) {
			if (nRelatedGcPadButton == aGameProfileConfig[i]) {
				valid = false;
			} else {
				i++;
			}
		}
		nGbaKey++;
	}
	return valid;
}

static void printProfileBuilder(int cursorPosition, int* aGameProfileConfig) {
	char selectedCursor[] = " <==";
	printf("\x1b[2J"); // clear the screen
	printf("\n=== Game profile builder ===\n\n");
	printf("\n   GBA Keys   |   NGC Pad");
	printf("\n______________|_____________");
	printf("\n              |\n");
	for (int i = 0; i < 6; i++) {
		char* sGbaKey = aGbaKeys[i];
		char sBlank[11];
		strcpy(sBlank, "");
		for (int j = 0; j < 11 - strlen(sGbaKey); j++) {
			strcat(sBlank, " ");
		}
		printf("   %s%s|   %s%s\n", sGbaKey, sBlank, aGcPadButtons[aGameProfileConfig[i]], i == cursorPosition ? selectedCursor : "");
	}
	printf("\n\nUP/DOWN: Navigate");
	printf("\nLEFT/RIGHT: Change mapping");
	printf("\n\nSELECT: Set default");
	if (isGameProfileValid(aGameProfileConfig)) {
		printf("\nSTART/A: Validate");
	} else {
		printf("\nError : invalid game profile");
	}
}

static void showHeader() {
	printf("\x1b[2J"); // clear the screen
	printf("\n=== GBA AS NGC CONTROLLER ===");
	printf("\nCreated by Extremscorner.org");
	printf("\nModified by Azlino (04-03-21)\n");
}

static int getPressedButtonsNumber() {
	unsigned buttons = ~REG_KEYINPUT;
	int nPressedButtons = 0;
	if (buttons & KEY_RIGHT) {
		nPressedButtons++;
	} else if (buttons & KEY_LEFT) {
		nPressedButtons++;
	}
	if (buttons & KEY_UP) {
		nPressedButtons++;
	} else if (buttons & KEY_DOWN) {
		nPressedButtons++;
	}
	if (buttons & KEY_A) {
		nPressedButtons++;
	}
	if (buttons & KEY_B) {
		nPressedButtons++;
	}
	if (buttons & KEY_L) {
		nPressedButtons++;
	}
	if (buttons & KEY_R) {
		nPressedButtons++;
	}
	if (buttons & KEY_START) {
		nPressedButtons++;
	}
	if (buttons & KEY_SELECT) {
		nPressedButtons++;
	}
	return nPressedButtons;
}

static void inputReleasedWait() {
	while (getPressedButtonsNumber() > 0) {
		VBlankIntrWait();
	}
}

static void printArt() {
 	printf("\n\n           ___------__");
	printf("\n     |\\__-- /\\       _-");
	printf("\n     |/    __      -");
	printf("\n     //\\  /  \\    /__");
	printf("\n     |  o|  0|__     --_");
	printf("\n     \\\\____-- __ \\   ___-");
	printf("\n     (@@    __/  / /_");
	printf("\n        -_____---   --_\n");
}

static int aCustomGameProfileConfig[6];
static int nTiming;
static int nGameProfile;
static bool bPrintKeys;
static bool softReset;
static bool hasMotor;
static int nSiCmdLen;
static int nProfileIterationGbaKey;
static int nProfileIterationGbaButtonState;
static unsigned gbaInput;
static unsigned previousGbaInput;

static void configureCustomProfile() {
	inputReleasedWait();
	// Entering game profile builder
	for (int i = 0; i < 6; i++) {
		aCustomGameProfileConfig[i] = aDefaultProfileConfig[i];
	}
	int cursorPosition = 0;
	bool validated = false;
	printProfileBuilder(cursorPosition, aCustomGameProfileConfig);
	while (!validated) {
		VBlankIntrWait();
		bool refreshed = false;
		unsigned gbaInput = ~REG_KEYINPUT;
		if ((gbaInput & KEY_START) || (gbaInput & KEY_A)) {
			// Validate
			if (isGameProfileValid(aCustomGameProfileConfig)) {
				validated = true;
			}
		} else if (gbaInput & KEY_SELECT) {
			// Set default mapping
			for (int i = 0; i < 6; i++) {
				aCustomGameProfileConfig[i] = aDefaultProfileConfig[i];
			}
			refreshed = true;
		} else if (gbaInput & KEY_UP) {
			if (cursorPosition > 0) {
				cursorPosition--;
				refreshed = true;
			}
		} else if (gbaInput & KEY_DOWN) {
			if (cursorPosition < 5) {
				cursorPosition++;
				refreshed = true;
			}
		} else if (gbaInput & KEY_RIGHT) {
			if (aCustomGameProfileConfig[cursorPosition] >= 11) {
				aCustomGameProfileConfig[cursorPosition] = 0;
			} else {
				aCustomGameProfileConfig[cursorPosition]++;
			}
			refreshed = true;
		} else if (gbaInput & KEY_LEFT) {
			if (aCustomGameProfileConfig[cursorPosition] == 0) {
				aCustomGameProfileConfig[cursorPosition] = 11;
			} else {
				aCustomGameProfileConfig[cursorPosition]--;
			}
			refreshed = true;
		}
		if (refreshed) {
			printProfileBuilder(cursorPosition, aCustomGameProfileConfig);
			inputReleasedWait();
		}
	}
}

static void printConfigurePrintKeys(bool bPrintKeys)
{
	showHeader();
	printf("\n==== Print Pressed Keys ====\n\n");
	printf("\nEnabled : %s", bPrintKeys ? "true" : "false");
	printf("\n\n\nRIGHT/LEFT: Change");
	printf("\n\nSTART/A: Validate");
	printf("\n\nWarning : this feature reduce the compatibility and might\nreduce the stability of this\nGBA as NGC controller !");
}

static int configurePrintKeys()
{
	bool validated = false;
	bool bPrintKeys = false;
	printConfigurePrintKeys(bPrintKeys);
	while (!validated) {
		VBlankIntrWait();
		unsigned buttons = ~REG_KEYINPUT;
		if ((buttons & KEY_START) || (buttons & KEY_A)) {
			validated = true;
		} else if ((buttons & KEY_LEFT) || (buttons & KEY_RIGHT)) {
			bPrintKeys = !bPrintKeys;
			printConfigurePrintKeys(bPrintKeys);
			inputReleasedWait();
		}
	}
	inputReleasedWait();
	return bPrintKeys;
}

static void printTimingSelect(int nTiming)
{
	showHeader();
	printf("\n======= Joybus config =======\n\n");
	printf("\nCurrent timing : ");
	printf("\n> %d (%.2f microseconds)", nTiming, 0.05959 * nTiming);
	printf("\n\n\nUP: +1 (slower)");
	printf("\nDOWN: -1 (faster)");
	printf("\n\nSELECT: Set default");
	printf("\nSTART/A: Validate");
}

static int timingSelect()
{
	int nTiming = 67;
	bool validated = false;
	printTimingSelect(nTiming);
	while (!validated) {
		VBlankIntrWait();
		bool refreshed = false;
		unsigned buttons = ~REG_KEYINPUT;
		if ((buttons & KEY_START) || (buttons & KEY_A)) {
			validated = true;
		} else if (buttons & KEY_SELECT) {
			nTiming = 67;
			refreshed = true;
		} else if (buttons & KEY_UP) {
			if (nTiming < 100) {
				nTiming++;
				refreshed = true;
			}
		} else if (buttons & KEY_DOWN) {
			if (nTiming > 50) {
				nTiming--;
				refreshed = true;
			}
		}
		if (refreshed)
		{
			printTimingSelect(nTiming);
			inputReleasedWait();
		}
	}
	nTiming = - nTiming;
	printf("\n\nTimer set to : %d", nTiming);
	inputReleasedWait();
	return nTiming;
}

static int profileSelect() {
	showHeader();
	printf("\nChoose a game profile :");
	printf("\nSELECT: Make custom profile");
	printf("\nA: %s", aGameProfilesNames[1]);
	printf("\nB: %s", aGameProfilesNames[2]);
	printf("\nL: %s", aGameProfilesNames[3]);
	printf("\nR: %s", aGameProfilesNames[4]);
	printf("\nUP: %s", aGameProfilesNames[5]);
	printf("\nRIGHT: %s", aGameProfilesNames[6]);
	int nGameProfile = -1;
	while (nGameProfile == -1) {
		VBlankIntrWait();
		unsigned buttons = ~REG_KEYINPUT;
		if (buttons & KEY_SELECT) {
			nGameProfile = 0; // Custom
		} else if (buttons & KEY_A) {
			nGameProfile = 1; // Default
		} else if (buttons & KEY_B) {
			nGameProfile = 2; // Super Smash Ultimate
		} else if (buttons & KEY_START) {
			nGameProfile = -1;
		} else if (buttons & KEY_L) {
			nGameProfile = 3; // Mario Kart Double Dash
		} else if (buttons & KEY_R) {
			nGameProfile = 4; // Mario Kart 8 Deluxe
		} else if (buttons & KEY_UP) {
			nGameProfile = 5; // New Super Mario Bros
		} else if (buttons & KEY_DOWN) {
			nGameProfile = -1;
		} else if (buttons & KEY_LEFT) {
			nGameProfile = -1;
		} else if (buttons & KEY_RIGHT) {
			nGameProfile = 6; // Mario Kart Wii
		}
	}
	if (nGameProfile == 0) {
		configureCustomProfile();
	}
	hasMotor = has_motor(); // Define motor
	printf("\n\nSelected game profile :\n> %s", aGameProfilesNames[nGameProfile]);
	inputReleasedWait();
	return nGameProfile;
}

int main(void)
{
	irqInit();
	irqEnable(IRQ_VBLANK);
	consoleSetup(1);
	if (getPressedButtonsNumber() > 0) {
		showHeader();
		printf("\nPlease release all buttons to\ncontinue...");
	}
	inputReleasedWait();
	bPrintKeys = configurePrintKeys();
	nTiming = timingSelect();
	nGameProfile = profileSelect();
	softReset = false;
	previousGbaInput = 0;
	
	RegisterRamReset(RESET_ALL_REG);

	consoleSetup(2);
	showHeader();
	printf("\nGame profile :");
	printf("\n> %s", aGameProfilesNames[nGameProfile]);
	printf("\nRumble : %s", hasMotor ? "Yes" : "No ");
	printArt();
	printf("\n\nPush A+B+SELECT+START to reset");

	REG_IE = IRQ_SERIAL | IRQ_TIMER2 | IRQ_TIMER1 | IRQ_TIMER0;
	REG_IF = REG_IF;

	REG_RCNT = R_GPIO | GPIO_IRQ | GPIO_SO_IO | GPIO_SO;

	REG_TM0CNT_L = nTiming;
	REG_TM1CNT_H = TIMER_START | TIMER_IRQ | TIMER_COUNT;
	REG_TM0CNT_H = TIMER_START;

	SoundBias(0);
	Halt();

	while (!softReset) {
		nSiCmdLen = SIGetCommand(buffer, sizeof(buffer) * 8 + 1);
		if (nSiCmdLen < 9) continue;

		gbaInput = ~REG_KEYINPUT;
		softReset = gbaInput == -1009; // Softreset A B START SELECT
		switch (nGameProfile) {
			case 1: // Default
				origin.buttons.a     = !!(gbaInput & KEY_A);
				origin.buttons.b     = !!(gbaInput & KEY_B);
				origin.buttons.start = !!(gbaInput & KEY_START);
				origin.buttons.z     = !!(gbaInput & KEY_SELECT);
				origin.buttons.l     = !!(gbaInput & KEY_L);
				origin.buttons.r     = !!(gbaInput & KEY_R);
				break;
			case 2: // Super Smash Ultimate
				origin.buttons.a     = !!(gbaInput & KEY_A);
				origin.buttons.b     = !!(gbaInput & KEY_B);
				origin.buttons.start = !!(gbaInput & KEY_START);
				origin.buttons.x     = !!(gbaInput & KEY_SELECT);
				origin.buttons.l     = !!(gbaInput & KEY_L);
				origin.buttons.z     = !!(gbaInput & KEY_R);
				break;
			case 3: // Mario Kart Double Dash
				origin.buttons.a     = !!(gbaInput & KEY_A);
				origin.buttons.z     = !!(gbaInput & KEY_B);
				origin.buttons.start = !!(gbaInput & KEY_START);
				origin.buttons.b     = !!(gbaInput & KEY_SELECT);
				origin.buttons.x     = !!(gbaInput & KEY_L);
				origin.buttons.r     = !!(gbaInput & KEY_R);
				break;
			case 4: // Mario Kart 8 Deluxe
				origin.buttons.a     = !!(gbaInput & KEY_A);
				origin.buttons.b     = !!(gbaInput & KEY_B);
				origin.buttons.start = !!(gbaInput & KEY_START);
				origin.buttons.x     = !!(gbaInput & KEY_SELECT);
				origin.buttons.l     = !!(gbaInput & KEY_L);
				origin.buttons.r     = !!(gbaInput & KEY_R);
				break;
			case 5: // New Super Mario Bros
				origin.buttons.a     = !!(gbaInput & KEY_A);
				origin.buttons.y     = !!(gbaInput & KEY_B);
				origin.buttons.start = !!(gbaInput & KEY_START);
				origin.buttons.b     = !!(gbaInput & KEY_SELECT);
				origin.buttons.l     = !!(gbaInput & KEY_L);
				origin.buttons.r     = !!(gbaInput & KEY_R);
				break;
			case 6: // Mario Kart Wii
				origin.buttons.a     = !!(gbaInput & KEY_A);
				origin.buttons.x     = !!(gbaInput & KEY_B);
				origin.buttons.start = !!(gbaInput & KEY_START);
				origin.buttons.l     = !!(gbaInput & KEY_L);
				origin.buttons.b     = !!(gbaInput & KEY_R);
				origin.buttons.up    = !!(gbaInput & KEY_UP);
				origin.buttons.down  = !!(gbaInput & KEY_DOWN);
				break;
			case 0: // Custom profile
				nProfileIterationGbaKey = 5;
				while (nProfileIterationGbaKey >= 0) {
					switch (nProfileIterationGbaKey) {
						case ID_GBAKEY_A:
						nProfileIterationGbaButtonState = !!(gbaInput & KEY_A);
						break;
						case ID_GBAKEY_B:
						nProfileIterationGbaButtonState = !!(gbaInput & KEY_B);
						break;
						case ID_GBAKEY_START:
						nProfileIterationGbaButtonState = !!(gbaInput & KEY_START);
						break;
						case ID_GBAKEY_SELECT:
						nProfileIterationGbaButtonState = !!(gbaInput & KEY_SELECT);
						break;
						case ID_GBAKEY_L:
						nProfileIterationGbaButtonState = !!(gbaInput & KEY_L);
						break;
						case ID_GBAKEY_R:
						nProfileIterationGbaButtonState = !!(gbaInput & KEY_R);
						break;
					}
					switch (aCustomGameProfileConfig[nProfileIterationGbaKey]) {
						case ID_GCPAD_A:
						origin.buttons.a = nProfileIterationGbaButtonState;
						break;
						case ID_GCPAD_B:
						origin.buttons.b = nProfileIterationGbaButtonState;
						break;
						case ID_GCPAD_X:
						origin.buttons.x = nProfileIterationGbaButtonState;
						break;
						case ID_GCPAD_Y:
						origin.buttons.y = nProfileIterationGbaButtonState;
						break;
						case ID_GCPAD_START:
						origin.buttons.start = nProfileIterationGbaButtonState;
						break;
						case ID_GCPAD_Z:
						origin.buttons.z = nProfileIterationGbaButtonState;
						break;
						case ID_GCPAD_L:
						origin.buttons.l = nProfileIterationGbaButtonState;
						break;
						case ID_GCPAD_R:
						origin.buttons.r = nProfileIterationGbaButtonState;
						break;
						case ID_GCPAD_UP:
						origin.buttons.up = nProfileIterationGbaButtonState;
						break;
						case ID_GCPAD_DOWN:
						origin.buttons.down = nProfileIterationGbaButtonState;
						break;
						case ID_GCPAD_LEFT:
						origin.buttons.left = nProfileIterationGbaButtonState;
						break;
						case ID_GCPAD_RIGHT:
						origin.buttons.right = nProfileIterationGbaButtonState;
						break;
					}
					nProfileIterationGbaKey--;
				}
				break;
		}
		
		origin.buttons.unknown = 
		id.status.unknown = origin.buttons.unknown;
		
		switch (buffer[0]) {
			case CMD_RESET:
				id.status.motor = MOTOR_STOP;
			case CMD_ID:
				if (nSiCmdLen == 9) {
					if (hasMotor) {
						id.type = 0x0900;
					} else {
						id.type = 0x2900;
					}
					SISetResponse(&id, sizeof(id) * 8);
				}
				break;
			case CMD_STATUS:
				if (nSiCmdLen == 25) {
					id.status.mode  = buffer[1];
					id.status.motor = buffer[2];
					status.buttons = origin.buttons;
					status.stick.x = origin.stick.x;
					status.stick.y = origin.stick.y;
					if (gbaInput & KEY_RIGHT)
						status.stick.x = origin.stick.x + 100;
					else if (gbaInput & KEY_LEFT)
						status.stick.x = origin.stick.x - 100;
					if (gbaInput & KEY_UP)
						status.stick.y = origin.stick.y + 100;
					else if (gbaInput & KEY_DOWN)
						status.stick.y = origin.stick.y - 100;
					switch (id.status.mode) {
						default:
							status.mode0.substick.x = origin.substick.x;
							status.mode0.substick.y = origin.substick.y;
							status.mode0.trigger.l  = (status.buttons.l ? 200 : origin.trigger.l) >> 4;
							status.mode0.trigger.r  = (status.buttons.r ? 200 : origin.trigger.r) >> 4;
							status.mode0.button.a   = (status.buttons.a ? 200 : origin.button.a) >> 4;
							status.mode0.button.b   = (status.buttons.b ? 200 : origin.button.b) >> 4;
							break;
						case 1:
							status.mode1.substick.x = origin.substick.x >> 4;
							status.mode1.substick.y = origin.substick.y >> 4;
							status.mode1.trigger.l  = (status.buttons.l ? 200 : origin.trigger.l);
							status.mode1.trigger.r  = (status.buttons.r ? 200 : origin.trigger.r);
							status.mode1.button.a   = (status.buttons.a ? 200 : origin.button.a) >> 4;
							status.mode1.button.b   = (status.buttons.b ? 200 : origin.button.b) >> 4;
							break;
						case 2:
							status.mode2.substick.x = origin.substick.x >> 4;
							status.mode2.substick.y = origin.substick.y >> 4;
							status.mode2.trigger.l  = (status.buttons.l ? 200 : origin.trigger.l) >> 4;
							status.mode2.trigger.r  = (status.buttons.r ? 200 : origin.trigger.r) >> 4;
							status.mode2.button.a   = (status.buttons.a ? 200 : origin.button.a);
							status.mode2.button.b   = (status.buttons.b ? 200 : origin.button.b);
							break;
						case 3:
							status.mode3.substick.x = origin.substick.x;
							status.mode3.substick.y = origin.substick.y;
							status.mode3.trigger.l  = (status.buttons.l ? 200 : origin.trigger.l);
							status.mode3.trigger.r  = (status.buttons.r ? 200 : origin.trigger.r);
							break;
						case 4:
							status.mode4.substick.x = origin.substick.x;
							status.mode4.substick.y = origin.substick.y;
							status.mode4.button.a   = (status.buttons.a ? 200 : origin.button.a);
							status.mode4.button.b   = (status.buttons.b ? 200 : origin.button.b);
							break;
					}
					SISetResponse(&status, sizeof(status) * 8);
				}
				break;
			case CMD_ORIGIN:
				if (nSiCmdLen == 9) SISetResponse(&origin, sizeof(origin) * 8);
				break;
			case CMD_RECALIBRATE:
			case CMD_STATUS_LONG:
				if (nSiCmdLen == 25) {
					id.status.mode  = buffer[1];
					id.status.motor = buffer[2];
					SISetResponse(&origin, sizeof(origin) * 8);
				}
				break;
		}
		set_motor(!softReset && id.status.motor == MOTOR_RUMBLE);
		if (softReset) {			
			// Reset all inputs to initial state
			// Fix key press not released when switching on a different profile
			origin.buttons.get_origin = 0;
			origin.buttons.use_origin = 0;
			origin.buttons.start      = 0;
			origin.buttons.a          = 0;
			origin.buttons.b          = 0;
			origin.buttons.x          = 0;
			origin.buttons.y          = 0;
			origin.buttons.l          = 0;
			origin.buttons.r          = 0;
			origin.buttons.z          = 0;
			origin.buttons.up         = 0;
			origin.buttons.down       = 0;
			origin.buttons.left       = 0;
			origin.buttons.right      = 0;
		} else if (bPrintKeys) {
			if (gbaInput != previousGbaInput) {
				// New input
				printf("\033[5;0H%s\033[5;0H%s%s%s%s%s%s%s%s%s%s", 
				"                          ",
				(gbaInput & KEY_A) ? "A " : "",
				(gbaInput & KEY_B) ? "B " : "",
				(gbaInput & KEY_START) ? "STA " : "",
				(gbaInput & KEY_SELECT) ? "SEL " : "",
				(gbaInput & KEY_L) ? "L " : "",
				(gbaInput & KEY_R) ? "R " : "",
				(gbaInput & KEY_UP) ? "UP " : "",
				(gbaInput & KEY_DOWN) ? "DOWN " : "",
				(gbaInput & KEY_LEFT) ? "LEFT " : "",
				(gbaInput & KEY_RIGHT) ? "RIGHT" : "");
			}
			previousGbaInput = gbaInput;
		}
	}
	RegisterRamReset(RESET_ALL_REG);
	main();
}
