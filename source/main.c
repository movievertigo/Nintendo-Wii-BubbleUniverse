#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <ogc/lwp_watchdog.h>
#include "sintable.h"

#define PANSPEED 2
#define SCALESPEED 1.005f
#define CURVECOUNT 256
#define CURVESTEP 4
#define ITERATIONS 1024
#define PI 3.1415926535897932384626433832795
#define SINTABLEPOWER 14
#define SINTABLEENTRIES (1<<SINTABLEPOWER)
#define ANG1INC (s32)((CURVESTEP * SINTABLEENTRIES) / 235)
#define ANG2INC (s32)((CURVESTEP * SINTABLEENTRIES) / (2*PI))

GXRModeObj	*videoMode;
u32 screenWidth, screenHeight;
u32* frameBuffer[2];
u32	fb = 0;

s32 SinTable[SINTABLEENTRIES];
u32 ColourTable[ITERATIONS*CURVECOUNT/CURVESTEP];

bool trails = false;
bool quit = false;
s32 speed = 0;
s32 oldSpeed = 0;
bool justReset = false;
float scaleFloat = 0.0f;
s32 scaleMul = 0;
float xFloat = 0.0f;
s32 xPan = 0;
float yFloat = 0.0f;
s32 yPan = 0;

inline void PlotYUV(u32* buffer, s32 xPos, s32 yPos, u8 y, u8 u, u8 v)
{
    u32* ptr = buffer + (yPos*screenWidth+xPos)/2;
    if (xPos&1)
    {
        *ptr = (*ptr & (0xFF<<24)) | (u<<16) | (y<<8) | v;
    }
    else
    {
        *ptr = (*ptr & (0xFF<<8)) | (u<<16) | (y<<24) | v;
    }
}

u32 RGBtoYUV(u8 r, u8 g, u8 b)
{
    u8 y = (u8)(0.257 * r + 0.504 * g + 0.098 * b + 16);
    u8 u = (u8)(-0.148 * r - 0.291 * g + 0.439 * b + 128);
    u8 v = (u8)(0.439 * r - 0.368 * g - 0.071 * b + 128);
    return (y << 16) | (u << 8) | v;
}

inline void ClearFrameBuffer(u32* ptr)
{
    u32* end = ptr + screenWidth*screenHeight/2;
    while (ptr < end)
    {
        *ptr++ = COLOR_BLACK;
    }
}

void ExpandSinTable()
{
    for (int i = 0; i < SINTABLEENTRIES/4; ++i)
    {
        SinTable[i] = SinTable[SINTABLEENTRIES/2 - i - 1] = compactsintable[i];
        SinTable[SINTABLEENTRIES/2 + i] = SinTable[SINTABLEENTRIES - i - 1] = -compactsintable[i];
    }
    for (int i = 0; i < SINTABLEENTRIES; ++i)
    {
        *(s16*)(SinTable+i) = *((s16*)(SinTable+((i+SINTABLEENTRIES/4)%SINTABLEENTRIES))+1);
    }
}

void InitColourTable()
{
    int colourIndex = 0;
    for (int i = 0; i < CURVECOUNT; i += CURVESTEP)
    {
        const u32 red = (256 * i) / CURVECOUNT;
        for (int j = 0; j < ITERATIONS; ++j)
        {
            const u32 green = 256*(1-(1-j/(float)ITERATIONS)*(1-j/(float)ITERATIONS));
            const u32 blue = (512-(red+green))>>1;
            ColourTable[colourIndex++] = RGBtoYUV(red, green, blue);
        }
    }
}

void ResetParams()
{
    xFloat = 0.0f;
    yFloat = 0.0f;
    scaleFloat = screenHeight/2*PI;
    xPan = xFloat + screenWidth/2;
    yPan = yFloat + screenHeight/2;
    scaleMul = scaleFloat;
    speed = 8;
    trails = false;
}

void Controls()
{
	WPAD_ScanPads();
    u32 buttonsDown = WPAD_ButtonsDown(0);
    u32 buttonsHeld = WPAD_ButtonsHeld(0);

	if (buttonsDown & WPAD_BUTTON_HOME) { quit = true; }
    if (buttonsHeld & WPAD_BUTTON_LEFT) { xFloat += PANSPEED; }
    if (buttonsHeld & WPAD_BUTTON_RIGHT) { xFloat -= PANSPEED; }
    if (buttonsHeld & WPAD_BUTTON_UP) { yFloat += PANSPEED; }
    if (buttonsHeld & WPAD_BUTTON_DOWN) { yFloat -= PANSPEED; }
	if (buttonsHeld & WPAD_BUTTON_B) { scaleFloat *= SCALESPEED; xFloat *= SCALESPEED; yFloat *= SCALESPEED; }
	if (buttonsHeld & WPAD_BUTTON_A) { scaleFloat /= SCALESPEED; xFloat /= SCALESPEED; yFloat /= SCALESPEED; }
	if (buttonsDown & WPAD_BUTTON_PLUS) { speed += 1; }
	if (buttonsDown & WPAD_BUTTON_MINUS) { speed -= 1; }

    if (!(buttonsHeld & (WPAD_BUTTON_1|WPAD_BUTTON_2))) { justReset = false; }
    if ((buttonsHeld & WPAD_BUTTON_1) && (buttonsHeld & WPAD_BUTTON_2))
    {
        ResetParams();
        justReset = true;
    }

    if ((buttonsDown & WPAD_BUTTON_1) && !justReset)
    {
        if (speed)
        {
            oldSpeed = speed;
            speed = 0;
        }
        else
        {
            speed = oldSpeed;
        }
    }
    if ((buttonsDown & WPAD_BUTTON_2) && !justReset) { trails = !trails; }

    xPan = xFloat + screenWidth/2;
    yPan = yFloat + screenHeight/2;
    scaleMul = scaleFloat;
}

int	main(void)
{
	VIDEO_Init();
	WPAD_Init();

	videoMode = VIDEO_GetPreferredMode(NULL);
    screenWidth = videoMode->fbWidth;
    screenHeight = videoMode->xfbHeight;

    ResetParams();

    for (int bufferIndex = 0; bufferIndex < 2; ++bufferIndex)
    {
    	frameBuffer[bufferIndex] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(videoMode));
        ClearFrameBuffer(frameBuffer[bufferIndex]);
    }

    CON_Init(frameBuffer[0], 0, 0, screenWidth, screenHeight, screenWidth * VI_DISPLAY_PIX_SZ);

	VIDEO_Configure(videoMode);
	VIDEO_SetNextFramebuffer(frameBuffer[fb]);
	VIDEO_SetBlack(false);
	VIDEO_Flush();

    ExpandSinTable();
    InitColourTable();

    s32 animationTime = 0;
	while (!quit)
	{
//        u64 startTime = gettime();

        u32* buffer = frameBuffer[fb];

        if (!trails)
        {
            ClearFrameBuffer(buffer);
        }

        s32 ang1Start = animationTime;
        s32 ang2Start = animationTime;

        u32* colourPtr = ColourTable;
        for (u32 i = 0; i < CURVECOUNT; i += CURVESTEP)
        {
            s32 x = 0, y = 0;
            for (u32 j = 0; j < ITERATIONS; ++j)
            {
                s32 values1, values2, pX, pY;

                values1 = SinTable[(ang1Start + x)&(SINTABLEENTRIES-1)];
                values2 = SinTable[(ang2Start + y)&(SINTABLEENTRIES-1)];
                x = (s32)(s16)values1 + (s32)(s16)values2;
                y = (values1>>16) + (values2>>16);
                pX = ((x * scaleMul) >> SINTABLEPOWER) + xPan;
                pY = ((y * scaleMul) >> SINTABLEPOWER) + yPan;

                if (pX >= 0 && pY >= 0 && pX < screenWidth && pY < screenHeight)
                {
                    PlotYUV(buffer, pX, pY, (*colourPtr)>>16, ((*colourPtr)>>8)&255, (*colourPtr)&255);
                }

                colourPtr++;
            }

            ang1Start += ANG1INC;
            ang2Start += ANG2INC;
        }

        Controls();

        animationTime += speed;

//        printf("\x1b[1;1H%0.3fms", 0.001*ticks_to_microsecs(gettime() - startTime));


		VIDEO_SetNextFramebuffer(buffer);
		VIDEO_Flush();
		VIDEO_WaitVSync();
        if (!trails)
        {
            fb ^= 1;
        }
	}

	return 0;
}
