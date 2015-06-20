/*
 * Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
 *
 * (c) Copyright 1996 - 2001 Gary Henderson (gary.henderson@ntlworld.com) and
 *                           Jerremy Koot (jkoot@snes9x.com)
 *
 * Super FX C emulator code 
 * (c) Copyright 1997 - 1999 Ivar (ivar@snes9x.com) and
 *                           Gary Henderson.
 * Super FX assembler emulator code (c) Copyright 1998 zsKnight and _Demo_.
 *
 * DSP1 emulator code (c) Copyright 1998 Ivar, _Demo_ and Gary Henderson.
 * C4 asm and some C emulation code (c) Copyright 2000 zsKnight and _Demo_.
 * C4 C code (c) Copyright 2001 Gary Henderson (gary.henderson@ntlworld.com).
 *
 * DOS port code contains the works of other authors. See headers in
 * individual files.
 *
 * Snes9x homepage: http://www.snes9x.com
 *
 * Permission to use, copy, modify and distribute Snes9x in both binary and
 * source form, for non-commercial purposes, is hereby granted without fee,
 * providing that this license information and copyright notice appear with
 * all copies and any derived work.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event shall the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Snes9x is freeware for PERSONAL USE only. Commercial users should
 * seek permission of the copyright holders first. Commercial use includes
 * charging money for Snes9x or software derived from Snes9x.
 *
 * The copyright holders request that bug fixes and improvements to the code
 * should be forwarded to them so everyone can benefit from the modifications
 * in future versions.
 *
 * Super NES and Super Nintendo Entertainment System are trademarks of
 * Nintendo Co., Limited and its subsidiary companies.
 */

#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <directfb.h>
#include <directfb_strings.h>

#include "snes9x.h"
#include "memmap.h"
#include "debug.h"
#include "ppu.h"
#include "snapshot.h"
#include "gfx.h"
#include "display.h"
#include "apu.h"
#include "unix.h"
#include "controls.h"
#include "conffile.h"
#include "blit.h"

#define COUNT(a) (sizeof(a) / sizeof(a[0]))

IDirectFB *dfb = NULL;
static IDirectFBSurface *screen = NULL;
static IDirectFBSurface *gfxscreen = NULL;
static DFBRectangle screenrec;
static DFBRectangle gfxscreenrec;
static DFBScreenPowerMode powerMode = DSPM_ON;
static DFBScreenEncoderConfig encoderCfg;
static int current_format = 0;

static int init = 0;
static uint8 *snes_buffer = NULL;

extern IDirectFBInputDevice *keyboard;
extern IDirectFBEventBuffer *keyboardbuffer;

extern int nr_joysticks;
extern IDirectFBInputDevice *joystick[];
extern IDirectFBEventBuffer *joystickbuffer[];

DirectFBScreenOutputResolutionNames(ResolutionNames);
DirectFBScreenEncoderScanModeNames(ScanModeNames);

#ifndef DirectFBScreenEncoderFrequencyNames
struct DFBScreenEncoderFrequencyName {
     DFBScreenEncoderFrequency frequency;
     const char *name;
};
#define DirectFBScreenEncoderFrequencyNames(Identifier) struct DFBScreenEncoderFrequencyName Identifier[] = { \
     { DSEF_25HZ, "25HZ" }, \
     { DSEF_29_97HZ, "29_97HZ" }, \
     { DSEF_50HZ, "50HZ" }, \
     { DSEF_59_94HZ, "59_94HZ" }, \
     { DSEF_60HZ, "60HZ" }, \
     { DSEF_75HZ, "75HZ" }, \
     { DSEF_UNKNOWN, "UNKNOWN" } \
}
#endif
DirectFBScreenEncoderFrequencyNames(FrequencyNames);

void S9xInitDisplay (int argc, char *argv[])
{
    DFBSurfaceDescription dsc;
    DFBSurfaceDescription dscgfx;
    int quit = 0;

    //
    // snes stuff
    //
    S9xBlitFilterInit();
    S9xBlit2xSaIFilterInit();
    S9xBlitHQ2xFilterInit();

    GFX.Pitch = SNES_WIDTH * 2 * 2;
    snes_buffer = (uint8 *) calloc(GFX.Pitch * ((SNES_HEIGHT_EXTENDED + 4) * 2), 1);
    if (!snes_buffer)
        fprintf(stderr, "Failed to allocate snes_buffer.");
    GFX.Screen = (uint16 *) (snes_buffer + (GFX.Pitch * 2 * 2));

    S9xSetRenderPixelFormat(RGB555);


    //
    // directfb stuff
    //
    DFBResult err;
    fprintf(stderr, "Trying to set video mode to 1920x1080 16\n");
    if (!dfb->SetVideoMode(dfb, 1920, 1080, 16)) {
        fprintf(stderr, "Trying to set video mode to 1360x768 16\n");
        DFBCHECK(dfb->SetVideoMode(dfb, 1360, 768, 32));
    }

    err = dfb->SetCooperativeLevel(dfb, DFSCL_FULLSCREEN);
    if (err)
        DirectFBError("Failed to get exclusive access", err);

    int screen_width, screen_height;
    fprintf(stderr, "Initiating screen\n");
    dsc.flags = DSDESC_CAPS;
    dsc.caps = (DFBSurfaceCapabilities)(DSCAPS_PRIMARY | DSCAPS_DOUBLE);
    DFBCHECK(dfb->CreateSurface(dfb, &dsc, &screen));

    DFBCHECK(screen->GetSize(screen, &screen_width, &screen_height));
    screenrec.x = screenrec.y = 0;
    screenrec.w = screen_width;
    screenrec.h = screen_height;
    fprintf(stderr, "Screen size: %dx%d\n", screen_width, screen_height);

    DFBCHECK(screen->FillRectangle(screen, 0, 0, screen_width, screen_height));
    
    fprintf(stderr, "Initiating dscgfx\n");
    dscgfx.flags = (DFBSurfaceDescriptionFlags)(DSDESC_HEIGHT | DSDESC_WIDTH | DSDESC_PREALLOCATED | DSDESC_PIXELFORMAT);
    dscgfx.width = SNES_WIDTH;
    dscgfx.height = SNES_HEIGHT;
    dscgfx.caps = DSCAPS_NONE;
    dscgfx.pixelformat = DSPF_RGB555;
    dscgfx.preallocated[0].data = snes_buffer;
    dscgfx.preallocated[0].pitch = GFX.Pitch;
    dscgfx.preallocated[1].data = NULL;
    dscgfx.preallocated[1].pitch = 0;
    int gfxsw, gfxsh;
    DFBCHECK(dfb->CreateSurface(dfb, &dscgfx, &gfxscreen));
    DFBCHECK(gfxscreen->GetSize(gfxscreen, &gfxsw, &gfxsh));
    DFBCHECK(screen->FillRectangle(gfxscreen, 0, 0, gfxsw, gfxsh));
    fprintf(stderr, "GFX Screen W: %d H: %d\n", gfxsw, gfxsh);

    gfxscreenrec.x = gfxscreenrec.y = 0;
    gfxscreenrec.w = gfxsw;
    gfxscreenrec.h = gfxsh;

    init = 1;
    
    S9xGraphicsInit();
}

void S9xDeinitDisplay ()
{
    if (init == 1) {
        fprintf(stderr, "Releasing display...\n");
        screen->Release(screen);
        gfxscreen->Release(gfxscreen);
        //primary_screen->Release(primary_screen);
        dfb->Release(dfb);
    
        free(snes_buffer);
    }
}

void S9xSetPalette ()
{
}

void S9xSetTitle (const char * /*title*/)
{
}

void S9xPutImage (int width, int height) {
    DFBCHECK(screen->StretchBlit(screen, gfxscreen, &gfxscreenrec, &screenrec));
    DFBCHECK(screen->Flip(screen, NULL, DSFLIP_ONSYNC));
}

const char * S9xStringInput (const char *) {
}

bool8 S9xMapDisplayInput (const char *n, s9xcommand_t *cmd)
{
    fprintf(stderr, "S9xMapDisplayInput %s\n", n);
}

s9xcommand_t S9xGetDisplayCommandT (const char *) {
    s9xcommand_t    cmd;

    cmd.type         = S9xBadMapping;
    cmd.multi_press  = 0;
    cmd.button_norpt = 0;
    cmd.port[0]      = 0xff;
    cmd.port[1]      = 0;
    cmd.port[2]      = 0;
    cmd.port[3]      = 0;

    return (cmd);
}

char * S9xGetDisplayCommandName (s9xcommand_t) {
}

const char * S9xParseDisplayConfig (ConfigFile &conf, int pass) {
    return ("Unix/X11");
}

void S9xExtraDisplayUsage (void) {
}

bool S9xDisplayPollButton (uint32 button, bool *pressed) {
    fprintf(stderr, "S9xDisplayPollButton %d\n", button);
}

bool S9xDisplayPollAxis (uint32, int16 *) {
    return false;
}

bool S9xDisplayPollPointer (uint32, int16 *, int16 *) {
    return false;
}

void S9xProcessEvents (bool8) {

    DFBInputEvent event;

    if (keyboard && keyboardbuffer) {
        while (keyboardbuffer->GetEvent(keyboardbuffer, DFB_EVENT(&event)) == DFB_OK)
        {
            bool key_down = (event.type == DIET_KEYPRESS);
            bool shift_down = (event.modifiers & DIMM_SHIFT);
            switch(event.type) {
                case DIET_KEYRELEASE:
                case DIET_KEYPRESS:
                         if (event.key_id == DIKI_R && shift_down && key_down) S9xReset();
                    else if (event.key_id == DIKI_ESCAPE  && key_down) S9xExit();
                    else if ((event.key_id >= DIKI_F1 && event.key_id <= DIKI_F9)  && key_down) {
                        int num = event.key_id - DIKI_F1;
                        char fname[256], ext[8];
                        sprintf(ext, ".00%d", num);
                        strcpy(fname, S9xGetFilename (ext, SNAPSHOT_DIR));
                        fprintf(stderr, "%s\n", fname);
                        if (shift_down)                    
                           S9xFreezeGame(fname);
                        else
                           S9xUnfreezeGame(fname);                  
                    }

                    else if (event.key_id == DIKI_L) S9xReportButton(SNES_TL_MASK, key_down);
                    else if (event.key_id == DIKI_R) S9xReportButton(SNES_TR_MASK, key_down);
                    else if (event.key_id == DIKI_W) S9xReportButton(SNES_X_MASK, key_down);
                    else if (event.key_id == DIKI_E) S9xReportButton(SNES_Y_MASK, key_down);
                    else if (event.key_id == DIKI_S) S9xReportButton(SNES_B_MASK, key_down);
                    else if (event.key_id == DIKI_D) S9xReportButton(SNES_A_MASK, key_down);
                    else if (event.key_id == DIKI_SPACE) S9xReportButton(SNES_START_MASK, key_down);
                    else if (event.key_id == DIKI_ENTER) S9xReportButton(SNES_SELECT_MASK, key_down);
                    else if (event.key_id == DIKI_UP) S9xReportButton(SNES_UP_MASK, key_down);
                    else if (event.key_id == DIKI_DOWN) S9xReportButton(SNES_DOWN_MASK, key_down);
                    else if (event.key_id == DIKI_LEFT) S9xReportButton(SNES_LEFT_MASK, key_down);
                    else if (event.key_id == DIKI_RIGHT) S9xReportButton(SNES_RIGHT_MASK, key_down);

                    // tv stuff
                    else if (event.key_id == DIKI_P && key_down) { // power mode
                        powerMode = (powerMode == DSPM_ON) ? DSPM_OFF : DSPM_ON;
                        fprintf(stderr, "SetPowerMode %s\n", (powerMode == DSPM_ON) ? "On" : "Off");
                        //primary_screen->SetPowerMode(primary_screen, powerMode);
                    }
                    else if (event.key_id == DIKI_O && key_down) { // output
                        DFBScreenOutputConfig cfg;
                        int width, height;
                        cfg.flags = DSOCONF_RESOLUTION;

                        if (current_format == 0) {
                            cfg.resolution = encoderCfg.resolution = DSOR_720_480;
                            encoderCfg.scanmode = DSESM_INTERLACED;
                            encoderCfg.frequency = DSEF_29_97HZ;
                            width = 720;
                            height = 480;
                        }
                        else if (current_format == 1) {
                            cfg.resolution = encoderCfg.resolution = DSOR_1920_1080;
                            encoderCfg.scanmode = DSESM_PROGRESSIVE;
                            encoderCfg.frequency = DSEF_50HZ;
                            width = 1920;
                            height = 1080;
                        }
                        else if (current_format == 2) {
                            cfg.resolution = encoderCfg.resolution = DSOR_1920_1080;
                            encoderCfg.scanmode = DSESM_PROGRESSIVE;
                            encoderCfg.frequency = DSEF_25HZ;
                            width = 1920;
                            height = 1080;
                        }
                        else if (current_format == 3) {
                            cfg.resolution = encoderCfg.resolution = DSOR_1920_1080;
                            encoderCfg.scanmode = DSESM_INTERLACED;
                            encoderCfg.frequency = DSEF_25HZ;
                            width = 1920;
                            height = 1080;
                        }
                        else if (current_format == 4) {
                            cfg.resolution = encoderCfg.resolution = DSOR_1280_720;
                            encoderCfg.scanmode = DSESM_PROGRESSIVE;
                            encoderCfg.frequency = DSEF_50HZ;
                            width = 1280;
                            height = 720;
                        }
                        else if (current_format == 5) {
                            cfg.resolution = encoderCfg.resolution = DSOR_720_576;
                            encoderCfg.scanmode = DSESM_INTERLACED;
                            encoderCfg.frequency = DSEF_25HZ;
                            width = 720;
                            height = 576;
                        }
                        else if (current_format == 6) {
                            cfg.resolution = encoderCfg.resolution = DSOR_1920_1080;
                            encoderCfg.scanmode = DSESM_PROGRESSIVE;
                            encoderCfg.frequency = DSEF_60HZ;
                            width = 1920;
                            height = 1080;
                        }
                        else if (current_format == 7) {
                            cfg.resolution = encoderCfg.resolution = DSOR_1920_1080;
                            encoderCfg.scanmode = DSESM_INTERLACED;
                            encoderCfg.frequency = DSEF_29_97HZ;
                            width = 1920;
                            height = 1080;
                        }
                        else {
                            cfg.resolution = encoderCfg.resolution = DSOR_1280_720;
                            encoderCfg.scanmode = DSESM_PROGRESSIVE;
                            encoderCfg.frequency = DSEF_60HZ;
                            width = 1280;
                            height = 720;
                        }

                        encoderCfg.flags = (DFBScreenEncoderConfigFlags)
                            (DSECONF_TV_STANDARD | DSECONF_SCANMODE | DSECONF_FREQUENCY | 
                             DSECONF_RESOLUTION | DSECONF_CONNECTORS);
                        encoderCfg.tv_standard = DSETV_DIGITAL;
                        encoderCfg.out_connectors = (DFBScreenOutputConnectors)(DSOC_HDMI | DSOC_VGA | DSOC_COMPONENT);
                        
                        unsigned int res = 0;
                        for(; res < sizeof(ResolutionNames); res++)
                            if (ResolutionNames[res].resolution == cfg.resolution) break;
                        unsigned int scan = 0;
                        for(; scan < sizeof(ScanModeNames); scan++)
                            if (ScanModeNames[scan].scan_mode == encoderCfg.scanmode) break;
                        unsigned int freq = 0;
                        for(; freq < sizeof(FrequencyNames); freq++)
                            if (FrequencyNames[freq].frequency == encoderCfg.frequency) break;

                        fprintf(stderr, "Encoder format changed to %s, %s, %s\n", 
                            ResolutionNames[res].name,
                            ScanModeNames[scan].name,
                            FrequencyNames[freq].name);

                        //(primary_screen->SetEncoderConfiguration(primary_screen, 0, &encoderCfg));

                        current_format = (current_format+1)%9;
                    }
                    break;

                default: break;
            }
        }
    }

#define MAX_JOY_BUTTONS 8 
#ifdef JOYSTICK_SUPPORT
    static const unsigned int button_code[MAX_JOY_BUTTONS+1] = {
        0,
        SNES_A_MASK,
        SNES_B_MASK,
        SNES_X_MASK,
        SNES_Y_MASK,
        SNES_TL_MASK,
        SNES_TR_MASK,
        SNES_START_MASK,
        SNES_SELECT_MASK
    };

    for (int i = 0; i < nr_joysticks; i++)
    {
        while (joystickbuffer[i]->GetEvent(joystickbuffer[i], DFB_EVENT(&event)) == DFB_OK)
        {
            bool button_down = (event.type == DIET_BUTTONPRESS);
            switch(event.type) {
                case DIET_BUTTONPRESS:
                case DIET_BUTTONRELEASE:
                    if (event.button < MAX_JOY_BUTTONS) {
                        S9xReportButton(button_code[event.button], button_down);
                    }
                    else if (event.button == 9 || event.button == 10) {
                        char fname[256], ext[8];
                        sprintf(ext, ".0%d", 10);
                        strcpy(fname, S9xGetFilename (ext, SNAPSHOT_DIR));
                        fprintf(stderr, "%s\n", fname);
                        if (event.button == 9)
                            S9xFreezeGame(fname);
                        else
                            S9xUnfreezeGame(fname);
                    }
                    else if (event.button == 11 && button_down) S9xExit();

                    break;

                case DIET_AXISMOTION:
                    if (event.axis == 0) {
                        S9xReportButton(SNES_LEFT_MASK, event.axisabs < 0);
                        S9xReportButton(SNES_RIGHT_MASK, event.axisabs > 0);
                    }
                    else if (event.axis == 1) {
                        S9xReportButton(SNES_UP_MASK, event.axisabs < 0);
                        S9xReportButton(SNES_DOWN_MASK, event.axisabs > 0);
                    }
                    break;

                default: break;
             }
        }

    }
#endif
}

void S9xHandleDisplayCommand (s9xcommand_t, int16, int16) {
}

void S9xParseDisplayArg (char **argv, int &ind, int)
{
}

void S9xMessage (int /* type */, int /* number */, const char *message)
{
    fprintf (stderr, "%s\n", message);
}

const char * S9xSelectFilename (const char *def, const char *dir1, const char *ext1, const char *title)
{
    static char s[PATH_MAX + 1];
    char        buffer[PATH_MAX + 1];

    //SetXRepeat(TRUE);

    printf("\n%s (default: %s): ", title, def);
    fflush(stdout);

    //SetXRepeat(FALSE);

    if (fgets(buffer, PATH_MAX + 1, stdin))
    {
        char    drive[_MAX_DRIVE + 1], dir[_MAX_DIR + 1], fname[_MAX_FNAME + 1], ext[_MAX_EXT + 1];

        char    *p = buffer;
        while (isspace(*p))
            p++;
        if (!*p)
        {
            strncpy(buffer, def, PATH_MAX + 1);
            buffer[PATH_MAX] = 0;
            p = buffer;
        }

        char    *q = strrchr(p, '\n');
        if (q)
            *q = 0;

        _splitpath(p, drive, dir, fname, ext);
        _makepath(s, drive, *dir ? dir : dir1, fname, *ext ? ext : ext1);

        return (s);
    }

    return (NULL);
}

void S9xGraphicsMode ()
{
}

void S9xTextMode ()
{
}


