// SaturnRenderDevice.hpp -- Sega Saturn render device-backend DECLARATION surface.
// True-port engine pivot (Task #196). Included from Graphics/Drawing.hpp via the
// RETRO_RENDERDEVICE_SATURN #elif branch, INSIDE namespace RSDK (no namespace is
// opened here) -- mirrors Graphics/SDL2/SDL2RenderDevice.hpp:1-3.
//
// WHY a hardware backend and not RSDK's software rasterizer:
//   tools/_portspike/qa_probe3_raster_budget.py measured RSDK's verbatim INK_NONE
//   inner loop at 11 SH-2 instructions/opaque pixel -> 788,480 cyc FLOOR for ONE
//   opaque full-screen vs the 446,667 cyc single-SH-2 60fps budget = 1.77x over at
//   the unreachable floor, for one of GHZ's 3-4 composited layers. So the Saturn
//   device backend renders through VDP1 sprites + VDP2 scroll planes (exactly what
//   the existing hand-port src/rsdk/ drawing layer already does at speed).
//
// This file is ONLY the declaration surface that lets the platform-INDEPENDENT
// logic core (Object/Scene/Collision/Animation/Storage/...) codegen to SH-2 .o.
// The RenderDevice::* method bodies (VDP1/VDP2) + the platform method-declaration
// set are added when Graphics/Drawing.cpp joins the Saturn compile target; the
// core TUs reference only ShaderEntry (shaderList[]) and RenderDevice::startVertex_*
// (via Drawing.hpp SetScreenVertices), both satisfied by the stubs below.

using ShaderEntry = ShaderEntryBase;

class RenderDevice : public RenderDeviceBase
{
};
