/*-------------------------------------------------------------------------*/
/* SaturnSGLArea.c -- P6.7d.2 (Task #210): ENGINE-SIZED SGL work area.      */
/*                                                                         */
/* Replaces the stock SGL_302j SGLAREA.O parameter block (decoded 2026-06- */
/* 11: SpriteBuf 127,152 B + CommandBuf 50,236 B + Pbuffer 40,000 B +      */
/* SortList 21,204 B sized for MaxPolygons=1761/MaxVertices=2500 3D        */
/* scenes, reserving 0x060C0000..0x06100000 = 256 KB). The engine true-    */
/* port draws ~70 VDP1 commands per frame (measured, qa_p6_draw D1), so    */
/* per the OFFICIAL sizing formulas this block is parameterized            */
/* MAX_POLYGONS=144 / MAX_VERTICES=384 at WORK_AREA=0x060F4000 -- freeing  */
/* 0x060C0000..0x060F4000 = 212,992 B of WRAM-H for the P6.8 code window   */
/* + packed collision (the W4 closer, platform/Saturn/SaturnMemoryMap.h).  */
/*                                                                         */
/* CITATIONS (read 2026-06-11 per the binding doc-first methodology):      */
/*   SGL302/DOC/210A_US/WORKAREA.TXT -- "the work RAM area (default mode:  */
/*     060C0000~) can be customized by defining the following variables.   */
/*     (...even if there is a partial change, all the variables must be    */
/*     set.)"; sizing: sec.1 SortList (sprites+6)*12 pow-2 region,         */
/*     TransList 20*12=240 fixed; sec.2 Zbuffer (128+128+256)*4 fixed,     */
/*     CLOfstBuf 32*32 fixed; sec.3 CommandBuf calls*~80+0x100             */
/*     (slDispSprite 36 B, slPutPolygon 72 B); sec.4 SpriteBuf             */
/*     (sprites+5)*36*2 banks; sec.5 Pbuffer only for slPutPolygon/        */
/*     slDispPolygon use; sec.6 MaxPolygons/MaxVertices bound the per-     */
/*     frame overflow CHECK (the Phase 2.3e sortlist-suppression class --  */
/*     gate G2 walks the live VDP1 command table against the ceiling).     */
/*   SGL302/SAMPLE/WORKAREA/WORKAREA.C -- the canonical customization      */
/*     form mirrored below (AdjWork enum layout + const variable set).     */
/*                                                                         */
/* Layout: WORK_AREA 0x060F4000 .. NextEntry 0x060FAF38 (28,472 B used of  */
/* the 30,720 B window below the STOCK TransList 0x060FB800, which stays   */
/* put -- 240 B used per sec.1). MasterStack 0x060FFC00 / SlaveStack /     */
/* PCM_Work unchanged from stock. EventBuf/WorkBuf ride .bss as in the     */
/* stock SLPROG section. Linked INSTEAD of SGLAREA.O via the make          */
/* command-line override:  make P6SCENE=1 SYSOBJS=platform/Saturn/         */
/* SaturnSGLArea.o  (jo_engine_makefile:224 appends SGLAREA.O with `+=`,   */
/* which a command-line definition overrides -- COMMON stays unmodified    */
/* per the CLAUDE.md hard rule).                                           */
/*-------------------------------------------------------------------------*/

#include "sl_def.h"

/*---- [1. Must not be modified] -------------------------------------------*/
#define SystemWork    0x060ffc00            /* System Variable             */

/*---- [2. Engine-image parameters] ----------------------------------------*/
#define MAX_VERTICES  384   /* >= 3x the polygon-emitter peak (~128 verts) */
#define MAX_POLYGONS  144   /* ~2x the measured ~70 VDP1 commands/frame    */
#define MAX_EVENTS    64    /* stock (jo/SGL event system)                 */
#define MAX_WORKS     192   /* F.4: 256->192 frees ~4.35KB WRAM-H .bss (WorkBuf
                             * below the ANIMPAK floor; sizeof(WORK)~68) to fund
                             * GHZSetup+BGSwitch WITHOUT moving any pool/globals;
                             * still > MAX_POLYGONS 144. MAX_WORKS=208 verified
                             * GREEN (player gate) -- 192 is the same class. */

#define WORK_AREA     0x060f4000            /* SGL Work Area (was 060C0000)*/

#define trans_list    0x060fb800            /* DMA Transfer Table (stock)  */
#define pcmbuf        0x25a78000            /* PCM Stream Address (stock)  */
#define PCM_SIZE      0x8000                /* PCM Stream Size (stock)     */

#define master_stack  SystemWork            /* MasterSH2 StackPointer      */
#define slave_stack   0x06001e00            /* SlaveSH2  StackPointer      */

/*---- [3. Macro] -----------------------------------------------------------*/
#define _Byte_        sizeof(Uint8)
#define _Word_        sizeof(Uint16)
#define _LongWord_    sizeof(Uint32)
#define _Sprite_      (sizeof(Uint16) * 18)

#define AdjWork(pt, sz, ct) (pt + (sz) * (ct))

/*---- [4. Work Area] -------------------------------------------------------*/
enum workarea {
    sort_list  = WORK_AREA,                                       /* 1,800 */
    zbuffer    = AdjWork(sort_list,  _LongWord_ * 3, MAX_POLYGONS + 6),
    spritebuf  = AdjWork(zbuffer,    _LongWord_, 512),            /* 2,048 */
    pbuffer    = AdjWork(spritebuf,  _Sprite_, (MAX_POLYGONS + 6) * 2),
    clofstbuf  = AdjWork(pbuffer,    _LongWord_ * 4, MAX_VERTICES),
    commandbuf = AdjWork(clofstbuf,  _Byte_ * 32 * 3, 32),        /* 3,072 */
    NextEntry  = AdjWork(commandbuf, _LongWord_ * 8, MAX_POLYGONS)
};
/* NextEntry = 0x060F4000 + 1800 + 2048 + 10800 + 6144 + 3072 + 4608
             = 0x060F4000 + 28,472 = 0x060FAF38  <  trans_list 0x060FB800 */

/*---- [5. Variable area] ---------------------------------------------------*/
const void*  MasterStack   = (void*)(master_stack);
const void*  SlaveStack    = (void*)(slave_stack);
const Uint16 MaxVertices   = MAX_VERTICES;
const Uint16 MaxPolygons   = MAX_POLYGONS;
const Uint16 EventSize     = sizeof(EVENT);
const Uint16 WorkSize      = sizeof(WORK);
const Uint16 MaxEvents     = MAX_EVENTS;
const Uint16 MaxWorks      = MAX_WORKS;
const void*  SortList      = (void*)(sort_list);
const Uint32 SortListSize  = (MAX_POLYGONS + 6) * _LongWord_ * 3;
const void*  Zbuffer       = (void*)(zbuffer);
const void*  SpriteBuf     = (void*)(spritebuf);
const Uint32 SpriteBufSize = _Sprite_ * (MAX_POLYGONS + 6) * 2;
const void*  Pbuffer       = (void*)(pbuffer);
const void*  CLOfstBuf     = (void*)(clofstbuf);
const void*  CommandBuf    = (void*)(commandbuf);
const void*  PCM_Work      = (void*)(pcmbuf);
const Uint32 PCM_WkSize    = PCM_SIZE;
const void*  TransList     = (void*)(trans_list);

EVENT  EventBuf[MAX_EVENTS];
WORK   WorkBuf[MAX_WORKS];
EVENT* RemainEvent[MAX_EVENTS];
WORK*  RemainWork[MAX_WORKS];

/*-------------------------------------------------------------------------*/
