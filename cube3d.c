/* ==========================================
   3D cube bouncing off its real projected bounds
   DOS mode 13h / Open Watcom 16-bit (C89)
   Build:  wcl -bt=dos -ml -3 -fp3 -ox cube3d.c

   SPACE / M  : mode (BOUNCE <-> FLOAT)
   ESC / Q    : quit
   ========================================== */

#include <i86.h>
#include <conio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <math.h>

#define W      320
#define H      200
#define SCRSZ  64000U

typedef struct { float x, y, z; } Vec3;
typedef struct { int   x, y;    } Vec2;

static unsigned char far *vid;
static unsigned char far *buf;

/* state shared by the projection */
static Vec3  rv[8];        /* rotated corner offsets (from the center) */
static Vec3  cam[8];       /* corner positions in camera space */
static Vec2  scr[8];       /* screen coordinates */
static int   sminX, smaxX, sminY, smaxY;   /* REAL projected bounds */
static float pcx, pcy, pcz, pscale;

/* ---------- video ---------- */

static void setMode(unsigned m)
{
    union REGS r;
    r.w.ax = m;
    int86(0x10, &r, &r);
}

static void setPal(int idx, int r, int g, int b)
{
    outp(0x3C8, idx);
    outp(0x3C9, r);
    outp(0x3C9, g);
    outp(0x3C9, b);
}

static void waitVsync(void)
{
    while (inp(0x3DA) & 8) ;
    while (!(inp(0x3DA) & 8)) ;
}

/* ---------- drawing ---------- */

static void hline(int x0, int x1, int y, unsigned char c)
{
    int x;
    unsigned base;

    if (y < 0 || y >= H) return;
    if (x0 > x1) { x = x0; x0 = x1; x1 = x; }
    if (x1 < 0 || x0 >= W) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= W) x1 = W - 1;

    base = (unsigned)y * (unsigned)W;
    for (x = x0; x <= x1; x++)
        buf[base + (unsigned)x] = c;
}

static void fillTri(int x0, int y0, int x1, int y1,
                    int x2, int y2, unsigned char col)
{
    int t, y, xa, xb;
    int dy01, dy12, dy02;

    if (y0 > y1) { t=x0; x0=x1; x1=t;  t=y0; y0=y1; y1=t; }
    if (y1 > y2) { t=x1; x1=x2; x2=t;  t=y1; y1=y2; y2=t; }
    if (y0 > y1) { t=x0; x0=x1; x1=t;  t=y0; y0=y1; y1=t; }

    if (y0 == y2) return;

    dy01 = y1 - y0;
    dy12 = y2 - y1;
    dy02 = y2 - y0;

    for (y = y0; y <= y2; y++) {
        if (y < 0)  continue;
        if (y >= H) break;

        xb = x0 + (int)((long)(x2 - x0) * (long)(y - y0) / (long)dy02);

        if (y < y1) {
            if (dy01 == 0) xa = x0;
            else xa = x0 + (int)((long)(x1 - x0) * (long)(y - y0) / (long)dy01);
        } else {
            if (dy12 == 0) xa = x1;
            else xa = x1 + (int)((long)(x2 - x1) * (long)(y - y1) / (long)dy12);
        }
        hline(xa, xb, y, col);
    }
}

/* ---------- projection + REAL bounds measurement ---------- */

static void project(void)
{
    int   i;
    float Z;

    sminX =  30000; smaxX = -30000;
    sminY =  30000; smaxY = -30000;

    for (i = 0; i < 8; i++) {
        cam[i].x = rv[i].x + pcx;
        cam[i].y = rv[i].y + pcy;
        cam[i].z = rv[i].z + pcz;

        Z = cam[i].z;
        if (Z < 0.5f) Z = 0.5f;

        scr[i].x = (int)((float)(W / 2) + (cam[i].x * pscale) / Z);
        scr[i].y = (int)((float)(H / 2) - (cam[i].y * pscale) / Z);

        if (scr[i].x < sminX) sminX = scr[i].x;
        if (scr[i].x > smaxX) smaxX = scr[i].x;
        if (scr[i].y < sminY) sminY = scr[i].y;
        if (scr[i].y > smaxY) smaxY = scr[i].y;
    }
}

/* ---------- geometry ---------- */

static int cubeV[8][3] = {
    {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
    {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}
};

static int faceQ[6][4] = {
    {0,3,2,1}, {4,5,6,7}, {0,4,7,3},
    {1,2,6,5}, {3,7,6,2}, {0,1,5,4}
};

static int faceRGB[6][3] = {
    {63, 26, 26}, {26, 63, 32}, {36, 40, 63},
    {63, 58, 26}, {58, 30, 63}, {26, 60, 63}
};

#define CUBE_HALF  0.80f

int main(void)
{
    float vx, vy, vz;
    float g, rest;
    float ax, ay, az;
    float sxr, cxr, syr, cyr, szr, czr;
    float Lx, Ly, Lz, Llen;
    float dt;
    float px, py, pz, t1, t2;
    float ux, uy, uz, wx, wy, wz;
    float nx, ny, nz, nlen, diff, facing;
    float k;                       /* pixel to world unit */

    int   i, f, l, lev, i0, i1, i2, i3;
    int   mode, ch, hit;
    unsigned char col;

    vid = (unsigned char far *)MK_FP(0xA000, 0);
    buf = (unsigned char far *)_fmalloc(SCRSZ);
    if (buf == 0) return 1;

    setMode(0x13);

    for (f = 0; f < 6; f++)
        for (l = 0; l < 16; l++)
            setPal(32 + f * 16 + l,
                   faceRGB[f][0] * l / 15,
                   faceRGB[f][1] * l / 15,
                   faceRGB[f][2] * l / 15);

    pscale = 170.0f;
    dt     = 1.0f / 70.0f;

    pcx = 0.0f;  pcy = 1.0f;  pcz = 6.0f;
    vx  = 2.4f;  vy  = 0.0f;  vz  = 0.7f;
    ax = ay = az = 0.0f;

    mode = 0;
    g    = -7.5f;
    rest =  0.90f;

    Lx = -0.45f; Ly = 0.80f; Lz = -0.40f;
    Llen = (float)sqrt(Lx*Lx + Ly*Ly + Lz*Lz);
    Lx /= Llen; Ly /= Llen; Lz /= Llen;

    for (;;) {

        /* --- keyboard --- */
        if (kbhit()) {
            ch = getch();
            if (ch == 27 || ch == 'q' || ch == 'Q') break;
            if (ch == ' ' || ch == 'm' || ch == 'M') {
                mode = 1 - mode;
                if (mode == 1) {                   /* FLOAT */
                    g = 0.0f;
                    rest = 1.0f;
                    if (vy > -0.6f && vy < 0.6f) vy = 1.2f;
                } else {                           /* BOUNCE */
                    g = -7.5f;
                    rest = 0.90f;
                }
            }
        }

        /* --- physics --- */
        vy += g * dt;
        pcx += vx * dt;
        pcy += vy * dt;
        pcz += vz * dt;

        /* z: no screen edge, world limits */
        if (pcz < 4.5f) { pcz = 4.5f; vz = -vz; }
        if (pcz > 8.0f) { pcz = 8.0f; vz = -vz; }

        /* --- rotating corner offsets --- */
        ax += 0.70f * dt;
        ay += 1.05f * dt;
        az += 0.45f * dt;

        sxr = (float)sin(ax); cxr = (float)cos(ax);
        syr = (float)sin(ay); cyr = (float)cos(ay);
        szr = (float)sin(az); czr = (float)cos(az);

        for (i = 0; i < 8; i++) {
            px = (float)cubeV[i][0] * CUBE_HALF;
            py = (float)cubeV[i][1] * CUBE_HALF;
            pz = (float)cubeV[i][2] * CUBE_HALF;

            t1 = py * cxr - pz * sxr;
            t2 = py * sxr + pz * cxr;
            py = t1; pz = t2;

            t1 =  px * cyr + pz * syr;
            t2 = -px * syr + pz * cyr;
            px = t1; pz = t2;

            t1 = px * czr - py * szr;
            t2 = px * szr + py * czr;
            px = t1; py = t2;

            rv[i].x = px;
            rv[i].y = py;
            rv[i].z = pz;
        }

        project();

        /* --- SCREEN EDGE COLLISION: using the real projected bounds --- */
        k   = pcz / pscale;      /* world units per pixel */
        hit = 0;

        if (smaxX > W - 1 && vx > 0.0f) {
            pcx -= (float)(smaxX - (W - 1)) * k;
            vx = -vx * rest;
            hit = 1;
        }
        if (sminX < 0 && vx < 0.0f) {
            pcx += (float)(-sminX) * k;
            vx = -vx * rest;
            hit = 1;
        }
        if (sminY < 0 && vy > 0.0f) {           /* top of screen = +y in the world */
            pcy -= (float)(-sminY) * k;
            vy = -vy * rest;
            hit = 1;
        }
        if (smaxY > H - 1 && vy < 0.0f) {       /* bottom of screen = -y in the world */
            pcy += (float)(smaxY - (H - 1)) * k;
            vy = -vy * rest;
            hit = 1;
        }

        if (hit) project();      /* project again after the correction */

        /* --- draw --- */
        _fmemset(buf, 0, SCRSZ);

        for (f = 0; f < 6; f++) {
            i0 = faceQ[f][0]; i1 = faceQ[f][1];
            i2 = faceQ[f][2]; i3 = faceQ[f][3];

            ux = cam[i1].x - cam[i0].x;
            uy = cam[i1].y - cam[i0].y;
            uz = cam[i1].z - cam[i0].z;

            wx = cam[i2].x - cam[i0].x;
            wy = cam[i2].y - cam[i0].y;
            wz = cam[i2].z - cam[i0].z;

            nx = uy * wz - uz * wy;
            ny = uz * wx - ux * wz;
            nz = ux * wy - uy * wx;

            facing = nx * cam[i0].x + ny * cam[i0].y + nz * cam[i0].z;
            if (facing >= 0.0f) continue;        /* back face */

            nlen = (float)sqrt(nx*nx + ny*ny + nz*nz);
            if (nlen < 0.000001f) nlen = 1.0f;
            nx /= nlen; ny /= nlen; nz /= nlen;

            diff = nx*Lx + ny*Ly + nz*Lz;
            if (diff < 0.0f) diff = 0.0f;
            if (diff > 1.0f) diff = 1.0f;

            lev = 4 + (int)(diff * 11.0f);
            if (lev > 15) lev = 15;
            col = (unsigned char)(32 + f * 16 + lev);

            fillTri(scr[i0].x, scr[i0].y, scr[i1].x, scr[i1].y,
                    scr[i2].x, scr[i2].y, col);
            fillTri(scr[i0].x, scr[i0].y, scr[i2].x, scr[i2].y,
                    scr[i3].x, scr[i3].y, col);
        }

        /* --- mode indicator --- */
        col = (mode == 0) ? 4 : 11;
        for (i = 4; i < 14; i++)
            hline(4, 13, i, col);

        waitVsync();
        _fmemcpy(vid, buf, SCRSZ);
    }

    setMode(0x03);
    _ffree(buf);
    return 0;
}
