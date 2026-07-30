#include "../tomb5/pch.h"
#include "function_table.h"
#include "polyinsert.h"
#include "function_stubs.h"
#include "dxshell.h"
#include "winmain.h"
#include "3dmath.h"

void (*AddQuadSorted)(D3DTLVERTEX* v, short v0, short v1, short v2, short v3, TEXTURESTRUCT* tex, long double_sided);
void (*AddTriSorted)(D3DTLVERTEX* v, short v0, short v1, short v2, TEXTURESTRUCT* tex, long double_sided);
void (*AddQuadZBuffer)(D3DTLVERTEX* v, short v0, short v1, short v2, short v3, TEXTURESTRUCT* tex, long double_sided);
void (*AddTriZBuffer)(D3DTLVERTEX* v, short v0, short v1, short v2, TEXTURESTRUCT* tex, long double_sided);
void (*AddLineSorted)(D3DTLVERTEX* v0, D3DTLVERTEX* v1, short drawtype);
bool (*IsVisible)(D3DTLVERTEX* v0, D3DTLVERTEX* v1, D3DTLVERTEX* v2);
HRESULT(*_BeginScene)();
HRESULT(*_EndScene)();

void InitialiseFunctionTable()
{
	_BeginScene = HWBeginScene;
	_EndScene = HWEndScene;
	IsVisible = _NVisible;

	if (App.dx.Flags & DXF_ZBUFFER)
	{
		AddQuadZBuffer = AddQuadClippedZBuffer;
		AddTriZBuffer = AddTriClippedZBuffer;
		AddQuadSorted = AddQuadClippedSorted;
		AddTriSorted = AddTriClippedSorted;
	}
	else
	{
		AddQuadZBuffer = AddQuadSubdivide;
		AddTriZBuffer = AddTriSubdivide;
		AddQuadSorted = AddQuadSubdivide;
		AddTriSorted = AddTriSubdivide;		
	}

	AddLineSorted = AddLineClippedSorted;
}

void HWInitialise()
{
	Log(__FUNCTION__);

	App.dx.lpD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);	//disable all stages
	App.dx.lpD3DDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	App.dx.lpD3DDevice->SetTextureStageState(2, D3DTSS_COLOROP, D3DTOP_DISABLE);
	App.dx.lpD3DDevice->SetTextureStageState(3, D3DTSS_COLOROP, D3DTOP_DISABLE);
	App.dx.lpD3DDevice->SetTextureStageState(4, D3DTSS_COLOROP, D3DTOP_DISABLE);
	App.dx.lpD3DDevice->SetTextureStageState(5, D3DTSS_COLOROP, D3DTOP_DISABLE);
	App.dx.lpD3DDevice->SetTextureStageState(6, D3DTSS_COLOROP, D3DTOP_DISABLE);
	App.dx.lpD3DDevice->SetTextureStageState(7, D3DTSS_COLOROP, D3DTOP_DISABLE);
	App.dx.lpD3DDevice->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU);

	// D3D9's default sampler address mode is WRAP, unlike the old fixed pipeline this game
	// was written against - with UI/font glyphs packed tightly into a shared texture page,
	// linear filtering at a glyph's UV edge would otherwise wrap and bleed in neighboring
	// glyphs (visible as ghosting/outline artifacts around menu text).
	App.dx.lpD3DDevice->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	App.dx.lpD3DDevice->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

	if (App.Filtering)
	{
		App.dx.lpD3DDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
		App.dx.lpD3DDevice->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
		App.dx.lpD3DDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	}
	else
	{
		App.dx.lpD3DDevice->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
		App.dx.lpD3DDevice->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
		App.dx.lpD3DDevice->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	}

	App.dx.lpD3DDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);	//multiply
	App.dx.lpD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
	App.dx.lpD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);	//texture color
	App.dx.lpD3DDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);	//diffuse color
	// The original decompile had this as D3DTA_TFACTOR ("texture factor(?)" - even the
	// decompiler wasn't sure), but D3DRS_TEXTUREFACTOR is never set anywhere in the whole
	// codebase, so it always sits at its opaque-white default - meaning alpha test/blend
	// never saw real per-pixel transparency at all. That produces exactly the symptom seen
	// here: whole rectangular texture cells render instead of just the masked shape (visible
	// as ghosting around font glyphs and a solid box around alpha-cutout foliage). The
	// correct source is almost certainly the texture's own alpha channel.
	App.dx.lpD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	App.dx.lpD3DDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);	//diffuse color
	App.dx.lpD3DDevice->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, D3DTSS_TCI_PASSTHRU);

	App.dx.lpD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, 0);	//disable alpha blending
	App.dx.lpD3DDevice->SetRenderState(D3DRS_SPECULARENABLE, 1);	//enable specular
	App.dx.lpD3DDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);	//no culling

	DXAttempt(App.dx.lpD3DDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE));	//z buffering
	DXAttempt(App.dx.lpD3DDevice->SetRenderState(D3DRS_ZWRITEENABLE, 1));

	App.dx.lpD3DDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);	//Fill solids.
	App.dx.lpD3DDevice->SetRenderState(D3DRS_DITHERENABLE, 1);	//enable dithering
	App.dx.lpD3DDevice->SetRenderState(D3DRS_ALPHAREF, 0);	//reference value for alpha testing
	App.dx.lpD3DDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_NOTEQUAL);	//accept pixels with different alpha(?)
	App.dx.lpD3DDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	App.dx.lpD3DDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	App.dx.lpD3DDevice->SetRenderState(D3DRS_ALPHATESTENABLE, 0);	//disables alpha tests

	DXAttempt(App.dx.lpD3DDevice->SetRenderState(D3DRS_AMBIENT, 0));
	DXAttempt(App.dx.lpD3DDevice->SetRenderState(D3DRS_COLORVERTEX, 0));

	App.dx.lpD3DDevice->SetRenderState(D3DRS_FOGENABLE, 0);

	// The game already does its own software transform/clip/light (see output.cpp,
	// polyinsert.cpp) and hands the device pre-transformed, pre-clipped D3DTLBUMPVERTEX
	// data (equivalent to the old D3DDP_DONOTCLIP per-DrawPrimitive flag, which DrawPrimitiveUP
	// has no equivalent for) - disable the D3D9 transform/clipping pipeline globally instead,
	// and set the one FVF used for every DrawPrimitiveUP call in the game (see HWDrawPrimitive).
	App.dx.lpD3DDevice->SetRenderState(D3DRS_CLIPPING, FALSE);
	App.dx.lpD3DDevice->SetFVF(FVF);
}

void HWDrawPrimitive(D3DPRIMITIVETYPE type, void* vertices, long numVerts)
{
	long primCount;

	primCount = (type == D3DPT_LINELIST || type == D3DPT_LINESTRIP) ? (numVerts / 2) : (numVerts / 3);

	if (primCount > 0)
		App.dx.lpD3DDevice->DrawPrimitiveUP(type, primCount, vertices, sizeof(D3DTLBUMPVERTEX));
}

HRESULT HWBeginScene()
{
	if (App.dx.InScene)
		Log("Already In Scene");

	App.dx.InScene = 1;
	App.dx.DoneBlit = 0;
	while (App.dx.WaitAtBeginScene) {};
	return App.dx.lpD3DDevice->BeginScene();
}

HRESULT HWEndScene()
{
	App.dx.InScene = 0;
	return App.dx.lpD3DDevice->EndScene();
}

bool _NVisible(D3DTLVERTEX* v0, D3DTLVERTEX* v1, D3DTLVERTEX* v2)
{
	return (v0->sy - v1->sy) * (v2->sx - v1->sx) - (v2->sy - v1->sy) * (v0->sx - v1->sx) < 0;
}

bool _Visible(D3DTLVERTEX* v0, D3DTLVERTEX* v1, D3DTLVERTEX* v2)
{
	return (v0->sy - v1->sy) * (v2->sx - v1->sx) - (v2->sy - v1->sy) * (v0->sx - v1->sx) > 0;
}

void SetCullCW()
{
	IsVisible = _Visible;
}

void SetCullCCW()
{
	IsVisible = _NVisible;
}
