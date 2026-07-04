//=============================================================================
// Native PSP sceGU pddi backend — shader/material. Mirrors pddi/gl/glmat.
//=============================================================================
#ifndef _GUMAT_HPP_
#define _GUMAT_HPP_

#include <pddi/pddi.hpp>
#include <pddi/base/baseshader.hpp>
#include <pddi/psp/gucon.hpp>

class pguTexture;

const int pguMaxPasses = 1;

struct pguTextureEnv
{
    bool enabled;
    pguTexture* texture;
    int uvSet;
    pddiUVMode uvMode;
    pddiFilterMode filterMode;
    bool alphaTest;
    pddiBlendMode alphaBlendMode;
    pddiCompareMode alphaCompareMode;
    float alphaRef;
    bool lit;
    bool twoSided;
    pddiShadeMode shadeMode;
    pddiColour diffuse;
    pddiColour specular;
    pddiColour ambient;
    pddiColour emissive;
    float shininess;
};

class pguMat : public pddiBaseShader
{
public:
    pguMat(pguContext*);
    ~pguMat();

    static pddiShadeColourTable colourTable[];
    static pddiShadeTextureTable textureTable[];
    static pddiShadeIntTable intTable[];
    static pddiShadeFloatTable floatTable[];

    const char* GetType(void);
    int         GetPasses(void);
    void        SetPass(int pass);

    pddiShadeTextureTable* GetTextureTable(void) { return textureTable; }
    pddiShadeIntTable*     GetIntTable(void)     { return intTable; }
    pddiShadeFloatTable*   GetFloatTable(void)   { return floatTable; }
    pddiShadeColourTable*  GetColourTable(void)  { return colourTable; }

    void SetTexture(pddiTexture* texture);
    void SetUVMode(int mode);
    void SetFilterMode(int mode);
    void SetShadeMode(int shade);
    void SetTwoSided(int);
    void EnableLighting(int);
    void SetDiffuse(pddiColour colour);
    void SetAmbient(pddiColour colour);
    void SetEmissive(pddiColour);
    void SetEmissiveAlpha(int);
    void SetSpecular(pddiColour);
    void SetShininess(float power);
    void SetBlendMode(int mode);
    void EnableAlphaTest(int b);
    void SetAlphaCompare(int compare);
    void SetAlphaRef(float ref);

    int  CountDevPasses(void);
    void SetDevPass(unsigned pass);

    // returns whether this material wants per-vertex lighting (normals) this pass
    bool IsLit(void) { return texEnv[0].lit; }
    bool HasTexture(void) { return texEnv[0].texture != NULL; }

protected:
    pguContext* context;
    pguTextureEnv texEnv[pguMaxPasses];
    int pass;
};

#endif
