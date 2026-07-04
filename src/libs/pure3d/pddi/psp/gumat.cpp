//=============================================================================
// Native PSP sceGU pddi backend — shader/material implementation.
//=============================================================================
#include <pddi/psp/gumat.hpp>
#include <pddi/psp/gutex.hpp>
#include <pddi/psp/gucon.hpp>

#include <pspgu.h>
#include <stdio.h>

pddiShadeColourTable pguMat::colourTable[] =
{
    {PDDI_SP_AMBIENT  , SHADE_COLOUR(&pguMat::SetAmbient)},
    {PDDI_SP_DIFFUSE  , SHADE_COLOUR(&pguMat::SetDiffuse)},
    {PDDI_SP_EMISSIVE , SHADE_COLOUR(&pguMat::SetEmissive)},
    {PDDI_SP_SPECULAR , SHADE_COLOUR(&pguMat::SetSpecular)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeTextureTable pguMat::textureTable[] =
{
    {PDDI_SP_BASETEX , SHADE_TEXTURE(&pguMat::SetTexture)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeIntTable pguMat::intTable[] =
{
    {PDDI_SP_UVMODE , SHADE_INT(&pguMat::SetUVMode)},
    {PDDI_SP_FILTER , SHADE_INT(&pguMat::SetFilterMode)},
    {PDDI_SP_SHADEMODE , SHADE_INT(&pguMat::SetShadeMode)},
    {PDDI_SP_ISLIT , SHADE_INT(&pguMat::EnableLighting)},
    {PDDI_SP_BLENDMODE , SHADE_INT(&pguMat::SetBlendMode)},
    {PDDI_SP_ALPHATEST , SHADE_INT(&pguMat::EnableAlphaTest)},
    {PDDI_SP_ALPHACOMPARE , SHADE_INT(&pguMat::SetAlphaCompare)},
    {PDDI_SP_TWOSIDED , SHADE_INT(&pguMat::SetTwoSided)},
    {PDDI_SP_EMISSIVEALPHA , SHADE_INT(&pguMat::SetEmissiveAlpha)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeFloatTable pguMat::floatTable[] =
{
    {PDDI_SP_SHININESS , SHADE_FLOAT(&pguMat::SetShininess)},
    {PDDI_SP_ALPHACOMPARE_THRESHOLD , SHADE_FLOAT(&pguMat::SetAlphaRef)},
    {PDDI_SP_NULL , NULL}
};

// pddiColour is 0xAARRGGBB; the GE wants 0xAABBGGRR (ABGR).
static inline unsigned GuColour(pddiColour c)
{
    return (unsigned(c.Alpha()) << 24) | (unsigned(c.Blue()) << 16) |
           (unsigned(c.Green()) << 8) | unsigned(c.Red());
}

pguMat::pguMat(pguContext* c)
{
    context = c;
    for(int i = 0; i < pguMaxPasses; i++)
    {
        texEnv[i].enabled = false;
        texEnv[i].texture = NULL;
        texEnv[i].uvSet = i;
        texEnv[i].uvMode = PDDI_UV_CLAMP;
        texEnv[i].filterMode = PDDI_FILTER_BILINEAR;
        texEnv[i].lit = false;
        texEnv[i].twoSided = false;
        texEnv[i].shadeMode = PDDI_SHADE_GOURAUD;
        texEnv[i].ambient.Set(255,255,255);
        texEnv[i].diffuse.Set(255,255,255);
        texEnv[i].specular.Set(0,0,0);
        texEnv[i].emissive.Set(0,0,0);
        texEnv[i].shininess = 0.0f;
        texEnv[i].alphaTest = false;
        texEnv[i].alphaCompareMode = PDDI_COMPARE_GREATEREQUAL;
        texEnv[i].alphaBlendMode = PDDI_BLEND_NONE;
        texEnv[i].alphaRef = 0.5f;
    }
    texEnv[0].enabled = true;
    pass = 0;
}

pguMat::~pguMat()
{
    for(int i = 0; i < pguMaxPasses; i++)
        if(texEnv[i].texture)
            ((pddiTexture*)texEnv[i].texture)->Release();
}

const char* pguMat::GetType(void) { static char s[] = "simple"; return s; }
int  pguMat::GetPasses(void) { return 1; }
void pguMat::SetPass(int p) { SetDevPass(p); }

void pguMat::SetTexture(pddiTexture* t)
{
    if(t == (pddiTexture*)texEnv[pass].texture) return;
    if(texEnv[pass].texture) ((pddiTexture*)texEnv[pass].texture)->Release();
    texEnv[pass].texture = (pguTexture*)t;
    if(texEnv[pass].texture) ((pddiTexture*)texEnv[pass].texture)->AddRef();
}

void pguMat::SetUVMode(int mode)      { texEnv[pass].uvMode = (pddiUVMode)mode; }
void pguMat::SetFilterMode(int mode)  { texEnv[pass].filterMode = (pddiFilterMode)mode; }
void pguMat::SetShadeMode(int shade)  { texEnv[pass].shadeMode = (pddiShadeMode)shade; }
void pguMat::SetTwoSided(int b)       { texEnv[pass].twoSided = b != 0; }
void pguMat::EnableLighting(int b)    { texEnv[pass].lit = b != 0; }
void pguMat::SetAmbient(pddiColour a) { texEnv[pass].ambient = a; }
void pguMat::SetDiffuse(pddiColour c) { texEnv[pass].diffuse = c; }
void pguMat::SetSpecular(pddiColour c){ texEnv[pass].specular = c; }

void pguMat::SetEmissive(pddiColour c)
{
    texEnv[pass].emissive = c;
    SetEmissiveAlpha(c.Alpha());
}

void pguMat::SetEmissiveAlpha(int alpha)
{
    texEnv[pass].diffuse.SetAlpha(alpha);
    int a = (alpha < 255) ? 0 : 255;
    texEnv[pass].specular.SetAlpha(a);
    texEnv[pass].ambient.SetAlpha(a);
    texEnv[pass].emissive.SetAlpha(a);
}

void pguMat::SetShininess(float power){ texEnv[pass].shininess = power; }
void pguMat::SetBlendMode(int mode)   { texEnv[pass].alphaBlendMode = (pddiBlendMode)mode; }
void pguMat::EnableAlphaTest(int b)   { texEnv[pass].alphaTest = b != 0; }
void pguMat::SetAlphaCompare(int cmp) { texEnv[pass].alphaCompareMode = (pddiCompareMode)cmp; }
void pguMat::SetAlphaRef(float ref)   { texEnv[pass].alphaRef = ref; }

int  pguMat::CountDevPasses(void) { return 1; }

// GE alpha-compare functions indexed by pddiCompareMode.
static const int guAlphaCmp[8] =
{
    GU_NEVER, GU_ALWAYS, GU_LESS, GU_LEQUAL, GU_GREATER, GU_GEQUAL, GU_EQUAL, GU_NOTEQUAL
};

void pguMat::SetDevPass(unsigned)
{
    int i = 0;

    if(texEnv[i].texture)
    {
        sceGuEnable(GU_TEXTURE_2D);
        texEnv[i].texture->SetGUState();   // binds sceGuTexImage / CLUT
        int f = (texEnv[i].filterMode == PDDI_FILTER_NONE) ? GU_NEAREST : GU_LINEAR;
        sceGuTexFilter(f, f);
        int wrap = (texEnv[i].uvMode == PDDI_UV_TILE) ? GU_REPEAT : GU_CLAMP;
        sceGuTexWrap(wrap, wrap);
        extern bool g_pspSkinnedDraw;   // TEST: ignore tex alpha for skinned chars
        sceGuTexFunc(GU_TFX_MODULATE, g_pspSkinnedDraw ? GU_TCC_RGB : GU_TCC_RGBA);
    }
    else
    {
        sceGuDisable(GU_TEXTURE_2D);
    }

    if(texEnv[i].alphaTest)
    {
        sceGuEnable(GU_ALPHA_TEST);
        sceGuAlphaFunc(guAlphaCmp[texEnv[i].alphaCompareMode], (int)(texEnv[i].alphaRef * 255.0f), 0xff);
    }
    else
    {
        sceGuDisable(GU_ALPHA_TEST);
    }

    // DIAG: dump the skin char's alpha/blend shader state vs a room draw's, to
    // find why Homer's material zeroes him out (room uses same context/depth).
    {
        extern bool g_pspSkinnedDraw;
        static int s_matSkin = 0, s_matRoom = 0;
        bool doLog = g_pspSkinnedDraw ? (s_matSkin < 4) : (s_matRoom < 4);
        if(doLog)
        {
            FILE* f = fopen("ms0:/shar_mat.log","a");
            if(f)
            {
                fprintf(f, "%s: lit=%d tex=%d alphaTest=%d cmp=%d ref=%.2f blendMode=%d diffuse=%08x ambient=%08x\n",
                        g_pspSkinnedDraw ? "SKIN" : "room", (int)texEnv[i].lit,
                        texEnv[i].texture ? 1 : 0, (int)texEnv[i].alphaTest,
                        (int)texEnv[i].alphaCompareMode, texEnv[i].alphaRef,
                        (int)texEnv[i].alphaBlendMode, GuColour(texEnv[i].diffuse), GuColour(texEnv[i].ambient));
                fclose(f);
            }
            if(g_pspSkinnedDraw) s_matSkin++; else s_matRoom++;
        }
    }

    switch(texEnv[i].alphaBlendMode)
    {
        case PDDI_BLEND_NONE:
            sceGuDisable(GU_BLEND);
            break;
        case PDDI_BLEND_ADD:
            sceGuEnable(GU_BLEND);
            sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xffffffff, 0xffffffff);
            break;
        default: // ALPHA and everything else -> standard alpha blend
            sceGuEnable(GU_BLEND);
            sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
            break;
    }

    // GE hardware lighting, mirroring the GL backend (glmat.cpp): only materials
    // flagged lit (characters like Homer) get GE lighting; unlit materials (the
    // flat cartoon room, prelit via vertex colour) keep texture x vertex colour.
    // The global ambient is issued each frame in pguContext::ApplyRenderState, and
    // the scene's directional/point lights are set via SetupHardwareLight — so a
    // lit surface = ambient*material + Σ light*material*N·L, then modulated by the
    // texture. This shades Homer instead of the previous fullbright-flat look,
    // without touching the room (whose materials are unlit).
    if(texEnv[i].lit)
    {
        // Material reflectances. sceGuModelColor takes emissive, ambient, diffuse,
        // specular. The GE also blends in the per-vertex colour (white for the
        // skinned char) as diffuse, so a white-vertex character reflects the light
        // colour directly.
        sceGuModelColor(GuColour(texEnv[i].emissive), GuColour(texEnv[i].ambient),
                        GuColour(texEnv[i].diffuse),  GuColour(texEnv[i].specular));
        sceGuEnable(GU_LIGHTING);
    }
    else
    {
        sceGuDisable(GU_LIGHTING);
    }

    // Cull stays off for now (winding unverified); depth is controlled by the
    // context's EnableZBuffer/SetZCompare state, not forced here any more.
    sceGuDisable(GU_CULL_FACE);
}
