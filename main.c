//====================================================
// hoser 2.0 - eink demo with all-new dither design
// (yet another "dithermation" demo by geekmaster)
// Copyright (C) 2012 by geekmaster, with MIT license:
// http://www.opensource.org/licenses/mit-license.php
//----------------------------------------------------
// This version adds the eink update method used in
// sparkle-1.0 (which is WHY it was so fast). On a K3
// there is such a HUGE speed increase, it deserved a
// major version number upgrade. K3 owners - rejoice!
// This was tested on DX,DXG,K3,K4(Mini),K5(Touch).
//----------------------------------------------------

#include <stdio.h>      // printf
#include <stdlib.h>    // malloc, free
#include <string.h>   // memset, memcpy
#include <unistd.h>  // usleep
#include <fcntl.h>  // open, close, write
#include <time.h>  // time
#include <sys/mman.h>   // mmap, munmap
#include <sys/ioctl.h> // ioctl
#include <linux/fb.h> // screeninfo
#include <png.h>

#define FBIO_EINK_UPDATE_DISPLAY_AREA 0x46dd
#define MAX_RAIN_FALLS_PER_LAYER    200
#define NUM_LAYERS 4
#define NUM_ACTIVE_LAYERS 4
#define MAX_U8  255
#define SPRITE_WIDTH1 20
#define SPRITE_HEIGHT1 25
#define FOLDER "/mnt/us/tcc/"

enum GMLIB_op { GMLIB_INIT,GMLIB_CLOSE,GMLIB_UPDATE };
typedef unsigned char u8;
typedef unsigned int u32;
typedef struct
{
    int x;    // top of next raindrop
    int y;    // left of next rain_drop
    u8 width;  // the width of the rain_drop
    u8 charHeight; // the height of the rain_drops
    int fallHeight; // the total height of the fall
    u8 nDrops; // number of rain_drop characters in this rain_fall
    u8 active;
    int lastCharSpriteRow; // the row location of the last sprite character displayed
} rainFall;

typedef struct
{
    u8 fallWidth; // the width of falls on this layer
    u8 charHeight; // the width of falls on this layer
    u8 maxFalls; // the maximum number of falls to draw on this layer
    u8 minDrops;
    rainFall falls[MAX_RAIN_FALLS_PER_LAYER]; // array to store the falls
    u8 *wb; // workbuffer for this layer
    u32 frequency; // how often this layer gets a new fall range 1 (always) to RAND_MAX (never)
    u8 *spriteSheet; // an array of bytes that this layer uses for sprites
    u8 **spriteSheetRows; // an array of pointers to the rows of the bytes that this layer uses for sprites
    int numSprites;
    u8 active;
} rainLayer;

// function prototypes
void initWorkbuffers();
void closeWorkbuffers();
void drawFall(rainFall *rf, u8* wb, u8** spriteSheetRows);
void drawLayer(rainLayer*);
void collapseLayers(rainLayer*);
void initLayer(rainLayer *);
inline int randomHallmist(int);
inline int randomLeft(int);
void addNewFallToLayer(rainLayer*, int);
void fill(int x,int y,int w, int h,int c, u8*);
void fill2(int x, int y, int w, int h, int c, u8*);
void drawSprite(int x,int y,int w, int h, u8 **rowPointers, u8* wb, unsigned int rowOffset);
void drawSpriteGrey(int x,int y,int w, int h, u8 **rowPointers, u8* wb, unsigned int rowOffset);
int loadSpriteSheet(const char* fileName, u8 ***spriteRowPointersOut, u8 **spriteDataOut, int *heightOut, int *widthOut);
void cleanup(void* spriteSheetRows, void* spriteSheet, void* rowPointers, void* pngImageData,
             png_structp pngPtr, png_infop infoPtr, FILE* pngFile);
void cleanupPngPointers(void* rowPointers, void* pngImageData,
             png_structp pngPtr, png_infop infoPtr, FILE* pngFile);
void fillByMemCpy(int x,int y,int w, int h, const u8 *rowPointer, u8* wb);

// Geekmaster's functions
void circle(int,int,int,u8*);
void box(int,int,int,int,u8*);
int gmlib(int);
inline void setpx(int,int,int,u8*);
void d4w(void);
void d8w(void);
void d8b(void);

// gmlib global vars
static const u8 dt[64] = {
    3,129,34,160,10,136,42,168,192,66,223,97,200,73,231,105,50,
    176,18,144,58,184,26,152,239,113,207,81,247,121,215,89,14,
    140,46,180,7,133,38,164,203,77,235,109,196,70,227,101,62,188,
    30,156,54,180,22,148,251,125,219,93,243,117,211,85 }; // 0-255 dither table
u8 *fb0=NULL;       // framebuffer pointer
u8 *mwb=NULL;       // main workbuffer
u8 *wb[NUM_LAYERS];       // layer workbuffer pointers
u32 mpu=100;      // msec/update
int fdFB=0;      // fb0 file descriptor
int fdMWB=0;      // main workbuffer file descriptor
int fdWB[NUM_LAYERS]; // layer workbuffer file discriptors
u32 fs=0;      // fb0 stride
u32 MX=0;     // xres (visible)
u32 MY=0;    // yres (visible)
u8 blk=0;   // black
u8 msk=0;  // black mask
u8 ppb=0; // pixels per byte

// MatrixCode global vars
static const u8 whiteSpriteLine[20] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
                                       255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
static const u8 blackSpriteLine[20] = {  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
                                         0,   0,   0,   0,   0,   0,   0,   0,   0,   0};
static const u8 greySpriteLine[20]  = { 63,  63,  63,  63,  63,  63,  63,  63,  63,  63,
                                        63,  63,  63,  63,  63,  63,  63,  63,  63,  63};
static const u8 numSprites = 53;

//===============================================
// hoser - eink demo showing all-new dither design
// This works on all kindle eink models.   Enjoy!
//-----------------------------------------------
void hoser(void) {
    u32 x,y,c,px1,py1,vx1,vy1,dx,dy,cc,cu,cl,i,l;
    //printf("start hoser\n");
    gmlib(GMLIB_INIT);      // init geekmaster functions
    c=0,px1=MX/2,py1=MY/2,vx1=1,vy1=5,cc=31,cl=16;
    rainLayer layers[NUM_LAYERS];

    layers[0].fallWidth = 5;
    layers[0].charHeight = 6;
    layers[0].maxFalls = 200;
    layers[0].minDrops = 30;
    layers[0].frequency = 1*layers[0].maxFalls;
    layers[0].active = 1;

    layers[1].fallWidth = 12;
    layers[1].charHeight = 15;
    layers[1].maxFalls = 200;
    layers[1].minDrops = 5;
    layers[1].frequency = 1*layers[1].maxFalls;
    layers[1].active = 1;

    layers[2].fallWidth = 16;
    layers[2].charHeight = 20;
    layers[2].maxFalls = 200;
    layers[2].minDrops = 10;
    layers[2].frequency = 5*layers[2].maxFalls;
    layers[2].active = 1;

    layers[3].fallWidth = 20;
    layers[3].charHeight = 25;
    layers[3].maxFalls = 200;
    layers[3].minDrops = 1;
    layers[3].frequency = 5*layers[3].maxFalls;
    layers[3].active = 1;

    //layers[4].fallWidth = 10;
    //layers[4].maxFalls = 20;
    //layers[4].frequency = 10*MAX_RAIN_FALLS_PER_LAYER;

    //printf("configure layers: %i\n",i);
    for (l=0; l<NUM_ACTIVE_LAYERS; l++)
    {
        initLayer(&layers[l]);
        layers[l].wb = wb[l];
    }

    //printf("preloop\n");
    fill(0,0, MX, MY, 255, mwb);
    gmlib(GMLIB_UPDATE); // update display
    fill(0,0, MX, MY, 127, mwb);
    gmlib(GMLIB_UPDATE); // update display
    fill(0,0, MX, MY, 0, mwb);
    gmlib(GMLIB_UPDATE); // update display

    for (i = 0; i < 1000; i++)
    {
        //printf("start iteration %i\n",i);
        for (l=0; l<NUM_ACTIVE_LAYERS; l++)
        {
            if (layers[l].active)
            {
                //printf("draw layer %i\n",l);
                drawLayer(&layers[l]);
            }
        }
        collapseLayers(layers);
        gmlib(GMLIB_UPDATE); // update display
        //printf("drew iteration %i\n",i);
    }
    /*for (cu=0;cu<20000;cu++) {
        if (0==cu%3000) { // periodic background display
          for (y=0; y<=MY/2; y++) for (x=0; x<=MX/2; x++) {
            dx=MX/2-x; dy=MY/2-y; c=255-(dx*dx+dy*dy)*255/(MX*220);
            setpx(x,y,c); setpx(MX-x,y,c);
            setpx(x,MY-y,c); setpx(MX-x,MY-y,c);
          }
        }
        circle(px1,py1,30); circle(px1,py1,31); circle(px1,py1,32);
        circle(px1,py1,29); circle(px1,py1,28); circle(px1,py1,27);
        circle(px1,py1,26); circle(px1,py1,25); circle(px1,py1,24);
        circle(px1,py1,23); circle(px1,py1,22); circle(px1,py1,21);
        circle(px1,py1,20); circle(px1,py1,19); circle(px1,py1,18);
        circle(px1,py1,17); circle(px1,py1,16); circle(px1,py1,15);
        circle(px1,py1,14); circle(px1,py1,13); circle(px1,py1,12);
        px1+=vx1; py1+=vy1;
        if (px1>MX-40 || px1<40) vx1=-vx1;
        if (py1<40) { py1=40; vy1=-vy1; }
        if (py1>MY-40) { py1=MY-40; vy1=-vy1; }
        if (0==cu%cl) { vy1++;
            gmlib(GMLIB_UPDATE); // update display
        }
        cc=(cc+4)%256; // cycle big box color
    }*/

// cleanup - close and free resources
    for (l=0; l<NUM_ACTIVE_LAYERS; l++)
    {
        // TODO: freeing spriteSheet at end of program causes segFault. Why?
        cleanup(layers[l].spriteSheetRows, /*layers[l].spriteSheet*/NULL, NULL, NULL, NULL, NULL, NULL);
    }
    gmlib(GMLIB_CLOSE); // close geekmaster functions
}

//====================================
// gmlib - geekmaster function library
// op (init, update, close)
//------------------------------------
int gmlib(int op) {
    struct update_area_t { int x1,y1,x2,y2,fx; u8 *buffer; } ua;
    struct fb_var_screeninfo screeninfo;
    static int fdUpdate=-1;
    if (GMLIB_INIT==op) {
        fdFB=open("/dev/fb0",O_RDWR);        // eink framebuffer
        ioctl(fdFB,FBIOGET_VSCREENINFO,&screeninfo);
        ppb=8/screeninfo.bits_per_pixel; // pixels per byte
        fs=screeninfo.xres_virtual/ppb; // fb0 stride
        blk=screeninfo.rotate-1;       // black
        MX=screeninfo.xres;           // max X+1
        MY=screeninfo.yres;          // max Y+1
        msk=1/ppb-1;                // black mask (4-bit=255,8-bit=0)
        fb0=(u8 *)mmap(0,MY*fs,PROT_READ|PROT_WRITE,MAP_SHARED,fdFB,0); // map fb0
        initWorkbuffers();
        fdUpdate=open("/proc/eink_fb/update_display",O_WRONLY);
    } else if (GMLIB_CLOSE==op) {
        gmlib(GMLIB_UPDATE); // update display
        munmap(fb0,MY*fs);  // unmap fb0
        close(fdUpdate);  // close update proc
        close(fdFB);     // close fb0
        closeWorkbuffers();
    } else if (GMLIB_UPDATE==op) {
        if (ppb/2) { d4w();
        	ua.x1=0; ua.y1=0; ua.x2=MX; ua.y2=MY; ua.fx=0; ua.buffer=NULL;
            ioctl(fdFB, FBIO_EINK_UPDATE_DISPLAY_AREA, &ua); // fx_update_partial
        }
        else if (blk) { d8w(); write(fdUpdate,"1\n",2);}
        else { d8b(); system("eips ''");}
    } else { return -1; }
    return fdUpdate;
}

//===============================
// initWorkbuffer - initializes a single workbuffer
// wb the workbuffer to init
//-------------------------------
void initWorkbuffers()
{
    int i;
    int ascii0 = 32; // the ascii code for 0
    char fileName[16] = {"/tmp/wb0"};

    // init main workbuffer
    fileName[8] = 'M';
    fdMWB=open(fileName,O_RDWR|O_CREAT); // work framebuffer
    lseek(fdMWB,MY*MX-1,SEEK_SET); write(fdMWB,"",1); // create main work buffer file
    mwb=(u8 *)mmap(0,MY*MX,PROT_READ|PROT_WRITE,MAP_SHARED,fdMWB,0); // map wb0
    memset(mwb, 0, MY*MX);

    // init layer workbuffers
    for (i=0; i<NUM_LAYERS; i++)
    {
        fileName[8] = ascii0+i;
        fdWB[i]=open(fileName,O_RDWR|O_CREAT); // work framebuffer
        lseek(fdWB[i],MY*MX-1,SEEK_SET); write(fdWB[i],"",1); // create work buffer0 file
        wb[i]=(u8 *)mmap(0,MY*MX,PROT_READ|PROT_WRITE,MAP_SHARED,fdWB[i],0); // map wb0
        memset(wb[i], 0, MY*MX);
    }

    //printf("finished init workbuffers\n");
}

//===============================
// closeWorkbuffer - closes a single workbuffer
// wb the workbuffer to close
//-------------------------------
void closeWorkbuffers()
{
    int i;
    for (i=0; i<NUM_LAYERS; i++)
    {
        munmap(wb[i],MY*MX);  // unmap wb
        close(fdWB[i]);    // close wb
    }
}

//===============================
// initLayer - initializes a single layer
// rL the rainLayer to update
//-------------------------------
void collapseLayers(rainLayer *layers)
{
    int i,x,y;
    u8 base, top, merge;
    u8 *pi1, *pi2, *pi3, *po;
    unsigned int i0, i1, i2, i3, ifinal;
    //printf("collapse layers\n");

    //copy the base layer into the workbuffer
    memcpy(mwb, wb[0], MY*MX);

    //merge the other layers
    //for (i=1; i<NUM_LAYERS; i++)
    //{
        pi3=wb[3];
        pi2=wb[2];
        pi1=wb[1];
        po=mwb;
        for (y=0;y<MY;y++)
        {
            for (x=0;x<MX;x++)
            {
                i0 = *po;
                i1 = *pi1++;
                i2 = *pi2++;
                i3 = *pi3++;
                ifinal = i0 + i1 + i2 + i3;
                *po++ = ifinal > MAX_U8 ? MAX_U8 : (u8)ifinal;
            }
        }
    //}
}

//===============================
// initLayer - initializes a single layer
// rL the rainLayer to update
//-------------------------------
void initLayer(rainLayer *rL)
{
    int i;
    char spriteFileName[256];
    int height = 0;
    int width = 0;

    for (i=0; i<rL->maxFalls; i++)
    {
        rL->falls[i].active = 0;
    }
    rL->spriteSheet = NULL;
    rL->spriteSheetRows = NULL;

    sprintf(spriteFileName, "%sSprites%d_%d.png", FOLDER, rL->fallWidth, rL->charHeight);
    loadSpriteSheet(spriteFileName, &rL->spriteSheetRows, &rL->spriteSheet, &height, &width);
    rL->numSprites = height / rL->charHeight;
}

//===============================
// addNewFallToLayer - adds a fall to a layer
// rL the rainLayer to update
// index the index of the fall to add
//-------------------------------
void addNewFallToLayer(rainLayer *rL, int index)
{
    rainFall *rF = &rL->falls[index];

    //rL->falls[index].x = randomLeft(rL->fallWidth);
    rF->x = randomLeft(rL->fallWidth);
    rF->y = 0;
    rF->width = rL->fallWidth;
    rF->charHeight = rL->charHeight;
    rF->nDrops = rL->minDrops + randomHallmist(30);
    rF->fallHeight = rL->charHeight * rF->nDrops;
    rF->active = 1;

    //printf("c:%i, d:%i, h:%i\n", rF->charHeight, rF->nDrops, rF->fallHeight);

    //printf("%i, %i\n", rL->falls[index].width, rL->falls[index].x);

    //printf("Added Fall: x%i, y%i, w%i, h%i, n%i\n", rL->falls[index].x, rL->falls[index].y,
    //        rL->falls[index].width, rL->falls[index].charHeight, rL->falls[index].nDrops);
}

//===============================
// drawLayer - draws the next step of a single layer
// rL the rainLayer to update
//-------------------------------
void drawLayer(rainLayer *rL)
{
    int i;
    rainFall *rf;
    for (i=0; i<rL->maxFalls; i++)
    {
        rf = &(rL->falls[i]);
        if (rf->active)
        {
            //printf("draw fall %i, active:%u\n", i,rf->active);
            drawFall(rf, rL->wb, rL->spriteSheetRows);

            // Turn off if off screen
            rf->active = rf->y <= (MY + rf->fallHeight);

            // increment to the next character position
            rf->y += rf->charHeight;
        }
        else
        {
            //if not active, chance of becoming active
            if (rand() < RAND_MAX / rL->frequency)
            {
                //printf("add\n");
                addNewFallToLayer(rL, i);
            }
        }
    }
}

//===============================
// randomLeft - calculates a random starting point
// width the width of the rainFall in pixels
//-------------------------------
inline int randomLeft(int width)
{

    // use MX / width to get the number of bins we can display
    // use randomHallmist to get a random bin
    // multiply the bin number by the bin width to get x
    return width * randomHallmist( MX/width);
}

//===============================
// randomHallmist - calculates a random integer value between 0 and the parameter
// max the maximum random value
//-------------------------------
inline int randomHallmist(int max)
{
    return rand() / (RAND_MAX / max);;
}

//===============================
// drawFall - draws the next step of a single fall
// rf the rainFall to update
//-------------------------------
void drawFall(rainFall *rf, u8* wb, u8** spriteSheetRows)
{
    int dx, dy, dwidth, dheight; //drawn box x,y,width,height
    const int margin = 0;
    dx = rf->x + margin;
    dy = rf->y + margin;
    dwidth = rf->width - 2 * margin;
    dheight = rf->charHeight - 2 * margin;
    int currentSpriteRow, spriteRowToGrey;

    spriteRowToGrey = rf->lastCharSpriteRow;
    //printf("%i, %i, %i, %i\n", rf->x, rf->y, rf->width, rf->charHeight);
    // Draw the next character if onscreen
    if ((rf->y) <= MY)
    {
        currentSpriteRow = randomHallmist(numSprites)*rf->charHeight;
        //    printf("w");
        //fillByMemCpy(dx, dy, dwidth, dheight, whiteSpriteLine, wb);
        drawSprite(dx, dy, dwidth, dheight, spriteSheetRows, wb, currentSpriteRow);

        rf->lastCharSpriteRow = currentSpriteRow;
        //fill(dx, dy, dwidth, dheight, 127, wb);
    }
    // Make the previous character grey
    if ((rf->y > 0) && (rf->y <= MY))
    {
    //    printf("g");
        //fill(dx, dy-rf->charHeight, dwidth, dheight, 63, wb);
        //fillByMemCpy(dx, dy-rf->charHeight, dwidth, dheight, greySpriteLine, wb);
        drawSpriteGrey(dx, dy-rf->charHeight, dwidth, dheight, spriteSheetRows, wb, spriteRowToGrey);

    }

    // black out the end of the rainfall
    if (rf->y > (rf->fallHeight))
    {
    //    printf("b");
        //fill(dx, dy - (rf->fallHeight) - rf->charHeight, dwidth, dheight, 0, wb);
        fillByMemCpy(dx, dy - rf->fallHeight - rf->charHeight, dwidth, dheight, blackSpriteLine, wb);
    }
    //printf("\n");
}

//========================================
// setpx - draw pixel to 8-bit work buffer
// x,y:screen coordinates, c:color(0-255).
//----------------------------------------
inline void setpx(int x,int y,int c, u8* wb) {
    wb[y*MX+x]=c;
}

//===========================
// d8b - dither 8-bit black 0
//---------------------------
void d8b(void) {
    u8 *pi,*po; int x,y;
    pi=mwb; po=fb0;
    for (y=0;y<MY;y++) {
        for (x=0;x<MX;x++) { *po++=dt[(y&7)*8|x&7]-*pi++>>8; }
        po+=(fs-MX);
    }
}

//===========================
// d8w - dither 8-bit white 0
//---------------------------
void d8w(void) {
    u8 *pi,*po; int x,y;
    pi=mwb; po=fb0;
    for (y=0;y<MY;y++) {
        for (x=0;x<MX;x++) { *po++=~(dt[(y&7)*8|x&7]-*pi++>>8); }
        po+=(fs-MX);
    }
}

//===========================
// d4w - dither 4-bit white 0
//---------------------------
void d4w(void) {
    u8 *pi,*po; int x,y,ys;
    pi=mwb; po=fb0;
    for (y=0;y<MY;y++) { ys=(y&7)*8;
        for (x=0;x<MX;x+=8) {
             *po++=(~(dt[ys]-*pi++>>8)|15)&(~(dt[ys+1]-*pi++>>8)|240);
             *po++=(~(dt[ys+2]-*pi++>>8)|15)&(~(dt[ys+3]-*pi++>>8)|240);
             *po++=(~(dt[ys+4]-*pi++>>8)|15)&(~(dt[ys+5]-*pi++>>8)|240);
             *po++=(~(dt[ys+6]-*pi++>>8)|15)&(~(dt[ys+7]-*pi++>>8)|240);
        }
    }
}

//==============================================
// circle - optimized midpoint circle algorithm
//----------------------------------------------
void circle(int cx,int cy,int r, u8* wb) {
    int e=-r,x=r,y=0;
    while (x>y) {
        setpx(cx+y,cy-x,255,wb); setpx(cx+x,cy-y,159,wb);
        setpx(cx+x,cy+y,95,wb); setpx(cx+y,cy+x,31,wb);
        setpx(cx-y,cy+x,0,wb); setpx(cx-x,cy+y,63,wb);
        setpx(cx-x,cy-y,127,wb); setpx(cx-y,cy-x,191,wb);
        e+=y; y++; e+=y;
        if (e>0) { e-=x; x-=1; e-=x; }
    }
}

//======================
// box - simple box draw
//----------------------
void box(int x,int y,int d,int c,u8* wb) {
    int i;
    for (i=0;i<d;++i) {
        setpx(x+i,y+d,c,wb); setpx(x+i,y-d,c,wb); setpx(x-i,y+d,c,wb); setpx(x-i,y-d,c,wb);
        setpx(x+d,y+i,c,wb); setpx(x+d,y-i,c,wb); setpx(x-d,y+i,c,wb); setpx(x-d,y-i,c,wb);
    }
}

//======================
// fill - fill an area
//----------------------
void fill(int x,int y,int w, int h,int c,u8* wb)
{
    int i,j;
    int dy;
    int a = (x+w <= MX) ? w : MX-x;
    int b = (y+h <= MY) ? h : MY-y;
    //printf("%i,%i,%i,%i,%i,%i\n",x,y,w,h,a,b);
    for (i=0; i<a; i++)
    {
        dy = y;
        for (j=0; j<b; j++)
        {
            setpx(x+i,dy++,c, wb);
        }
    }
}

//======================
// fillByMemCpy - fill an area
//----------------------
void fillByMemCpy(int x,int y,int w, int h, const u8 *rowPointer, u8* wb)
{
    int i,j;
    int a = (x+w <= MX) ? w : MX-x;
    int b = (y+h <= MY) ? h : MY-y;
    u8 *po = wb;

    //printf("%i,%i,%i,%i,%i,%i\n",x,y,w,h,a,b);
    po += y*MX + x;
    for (i=0; i<b; i++)
    {
        memcpy(po, rowPointer, a);
        po += MX;
    }
}

//======================
// drawSprite - draw a character
//----------------------
void drawSprite(int x,int y,int w, int h, u8 **rowPointers, u8* wb, unsigned int rowOffset)
{
    int i,j;
    int a = (x+w <= MX) ? w : MX-x;
    int b = (y+h <= MY) ? h : MY-y;
    u8 *po = wb;

    //printf("%i,%i,%i,%i,%i,%i,%i\n",x,y,w,h,a,b, rowOffset);
    po += y*MX + x;
    for (i=0; i<b; i++)
    {
        memcpy(po, rowPointers[rowOffset+i], a);
        po += MX;
    }
}

//======================
// drawSpriteGrey - draw a character, but grey it out
//----------------------
void drawSpriteGrey(int x,int y,int w, int h, u8 **rowPointers, u8* wb, unsigned int rowOffset)
{
    int i,j;
    int a = (x+w <= MX) ? w : MX-x;
    int b = (y+h <= MY) ? h : MY-y;
    u8 *po = wb;
    int temp;

    //printf("%i,%i,%i,%i,%i,%i,%i\n",x,y,w,h,a,b, rowOffset);
    po += y*MX + x;
    for (i=0; i<b; i++)
    {
        for(j=0; j<a; j++)
        {
            temp = rowPointers[rowOffset][j]; - 128;
            temp -= 128;
            *po++ = temp < 0 ? 0 : (u8)temp;
        }
        //memcpy(po, rowPointers[rowOffset+i], a);
        po = wb + (y+i) * MX + x;
        rowOffset++;
    }
}

//======================
// loadSpriteSheet - load sprites from file
//----------------------
int loadSpriteSheet(const char* fileName, u8 ***spriteRowPointersOut, u8 **spriteDataOut, int *heightOut, int *widthOut)
{
    u8 signature[8];
    png_uint_32  width, height, bytesPerRow;
    int  bit_depth, color_type, i;
    FILE *pngFile = NULL;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;
    u8 *pngImage_data = NULL;
    u8 *spriteSheet = NULL;
    u8 **spriteSheetRows = NULL;
    png_bytepp rowPointers = NULL;

    // open and double check file is a PNG
    //printf("Trying to open %s\n", fileName);
    pngFile = fopen(fileName, "rb");
    fread(signature, 1, 8, pngFile);
    if (png_sig_cmp(signature, 0, 8))
    {
        return 1;
    }

    // create PNG structs
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
    {
        cleanup(spriteSheetRows, spriteSheet, rowPointers, pngImage_data, png_ptr, info_ptr, pngFile);
        return 4;   /* out of memory */
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        cleanup(spriteSheetRows, spriteSheet, rowPointers, pngImage_data, png_ptr, info_ptr, pngFile);
        return 4;   /* out of memory */
    }

    // set png error action
    if (setjmp(png_jmpbuf(png_ptr)))
    {
        cleanup(spriteSheetRows, spriteSheet, rowPointers, pngImage_data, png_ptr, info_ptr, pngFile);
        return 2;
    }

    png_init_io(png_ptr, pngFile);
    png_set_sig_bytes(png_ptr, 8);  /* we already read the 8 signature bytes */

    png_read_info(png_ptr, info_ptr);  /* read all PNG info up to image data */

    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
      NULL, NULL, NULL);
    printf("w:%d, h%d, bitdepth:%d, colortype:%d\n", width, height, bit_depth, color_type);

    //png_read_update_info(png_ptr, info_ptr);
    bytesPerRow = png_get_rowbytes(png_ptr, info_ptr);

    if ((pngImage_data = (unsigned char *)malloc(bytesPerRow*height)) == NULL)
    {
        cleanup(spriteSheetRows, spriteSheet, rowPointers, pngImage_data, png_ptr, info_ptr, pngFile);
        return 4;   /* out of memory */
    }
    if ((rowPointers = (png_bytepp)malloc(height*sizeof(png_bytep))) == NULL)
    {
        cleanup(spriteSheetRows, spriteSheet, rowPointers, pngImage_data, png_ptr, info_ptr, pngFile);
        return 4;   /* out of memory */
    }

    unsigned char *pi = pngImage_data;
    png_bytepp pr = rowPointers;
    // set the row pointers up
    for (i=0; i < height; i++)
    {
        pr[i] = pi;
        pi += bytesPerRow;
    }

    png_read_image(png_ptr, rowPointers);

    if ((spriteSheet = (unsigned char*)malloc(height*width)) == NULL)
    {
        cleanup(spriteSheetRows, spriteSheet, rowPointers, pngImage_data, png_ptr, info_ptr, pngFile);
        return 4;  /* out of memory */
    }
    if ((spriteSheetRows = (unsigned char **)malloc(height*sizeof(unsigned char **))) == NULL)
    {
        cleanup(spriteSheetRows, spriteSheet, rowPointers, pngImage_data, png_ptr, info_ptr, pngFile);
        return 4;  /* out of memory */
    }

    int j, y;
    y=0;
    unsigned char *po = spriteSheet;
    unsigned char **ro = spriteSheetRows;
    pi = pngImage_data;
    for (i=0; i < height; i++)
    {
        //printf("Row %d\n", i);
        *ro++ = po;
        for(j=0; j < width; j++)
        {
            //printf("%d:%d ", j, row_pointers[y+i][j++]);
            *po++ = *pi++;
            // alternate way possible is to make pi a unsigned 16bit
            // and then bit shift the alpha channel out
            if (*pi++ != 255)
            {
                printf("Unknown Alpha?\n");
                cleanup(spriteSheetRows, spriteSheet, rowPointers, pngImage_data, png_ptr, info_ptr, pngFile);
            }

            //printf("\n");
            //printf("%d:%d \n", j, row_pointers[y+i][j]);
        }
        printf("\n\n");
    }
    png_read_end(png_ptr, NULL);
    cleanupPngPointers(rowPointers, pngImage_data, png_ptr, info_ptr, pngFile);
    *spriteDataOut = spriteSheet;
    *spriteRowPointersOut = spriteSheetRows;
    *heightOut = height;
    *widthOut = width;
    return 0;
}

void cleanupPngPointers(void* rowPointers, void* pngImageData,
             png_structp pngPtr, png_infop infoPtr, FILE* pngFile)
{
    if (rowPointers)
    {
        free(rowPointers);
        rowPointers = NULL;
    }
    if (pngImageData)
    {
        free(pngImageData);
        pngImageData = NULL;
    }
    png_destroy_read_struct(&pngPtr, &infoPtr, NULL);
    if (pngFile)
    {
        fclose(pngFile);
        pngFile = NULL;
    }
}

void cleanup(void* spriteSheetRows, void* spriteSheet, void* rowPointers, void* pngImageData,
             png_structp pngPtr, png_infop infoPtr, FILE* pngFile)
{
    if (spriteSheetRows)
    {
        free(spriteSheetRows);
        spriteSheetRows = NULL;
    }
    if (spriteSheet)
    {
        free(spriteSheet);
        spriteSheet = NULL;
    }
    cleanupPngPointers(rowPointers, pngImageData, pngPtr, infoPtr, pngFile);
}

//==================
// main - start here
//------------------
int main(int argc,char **argv) {
    if (argc>1) { mpu=atoi(argv[1]); }
    //printf("main\n");
    hoser(); // do the hoser demo :D
    return 0;
}
