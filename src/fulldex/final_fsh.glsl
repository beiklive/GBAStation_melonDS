#version 460
// Adapted from Pokémon Fulldex / melonDS (GPL-3.0-or-later).
// ssrecomp, GPU3D_WideGpu.h: the picture in the form GPU3D_Wide.cpp's own rasteriser leaves
// it (XRGB, the top byte the alpha: 0 where nothing was drawn), with the fog.

layout (std140, binding = 0) uniform Frame
{
    uvec4 uSize;
    vec4 uScale;
    uvec4 uFog;           // fog colour, offset, shift, flags (bit 0 fog on, bit 1 fog colours)
    uvec4 uToon[8];
    uvec4 uEdges[2];
    uvec4 uFogDensity[9];
};

layout (binding = 0) uniform sampler2D uColor;
layout (binding = 1) uniform usampler2D uAttr;

layout (location = 0) out uint oColor;

uint Density(uint i)
{
    uvec4 e = uFogDensity[i >> 2];
    uint j = i & 3u;
    return j == 0u ? e.x : j == 1u ? e.y : j == 2u ? e.z : e.w;
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 c = texelFetch(uColor, p, 0);
    uint stored = uint(c.a * 255.0 + 0.5);
    if (stored == 0u)
    {
        // A pixel left out between two polygons that meet (the graphics chip's rules for which
        // pixels an edge covers are not the DS's: dotted black lines along the seams of
        // SoulSilver's ground): the pixel beside it, when there is something drawn on both sides
        // of it, across or up and down. (Nothing drawn on one side is the end of the world.)
        ivec2 last = ivec2(uSize.xy) - 1;
        ivec2 l = clamp(p - ivec2(1, 0), ivec2(0), last), r = clamp(p + ivec2(1, 0), ivec2(0), last);
        ivec2 u = clamp(p - ivec2(0, 1), ivec2(0), last), d = clamp(p + ivec2(0, 1), ivec2(0), last);
        vec4 cl = texelFetch(uColor, l, 0), cr = texelFetch(uColor, r, 0);
        vec4 cu = texelFetch(uColor, u, 0), cd = texelFetch(uColor, d, 0);
        if (l != p && r != p && cl.a > 0.0 && cr.a > 0.0)
        {
            p = l;
            c = cl;
        }
        else if (u != p && d != p && cu.a > 0.0 && cd.a > 0.0)
        {
            p = u;
            c = cu;
        }
        else
        {
            oColor = 0u;
            return;
        }
        stored = uint(c.a * 255.0 + 0.5);
    }
    uvec3 rgb = uvec3(c.rgb * 255.0 + 0.5);
    // (alpha + 1) / 32 back to alpha, and to 8 bits.
    uint a = uint(float(stored) * 32.0 / 255.0 + 0.5) - 1u;


    uvec2 attr = texelFetch(uAttr, p, 0).xy;
    uint depth = attr.x & 0xffffffu;
    uint polyId = attr.x >> 24;
    if ((uSize.z & 32u) != 0u) {
        const ivec2 delta[4] = ivec2[4](ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1));
        for (int i=0;i<4;++i) {
            ivec2 q = p + delta[i];
            if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, ivec2(uSize.xy)))) continue;
            uint neighbour = texelFetch(uAttr, q, 0).x;
            if ((neighbour >> 24) != polyId && (neighbour & 0xffffffu) > depth) {
                uint color = uEdges[polyId >> 5][(polyId >> 3) & 3u];
                uvec3 edge = uvec3(color, color >> 5, color >> 10) & 31u;
                rgb = (edge << 3) | (edge >> 2);
                break;
            }
        }
    }
    if ((uFog.w & 1u) != 0u && attr.y != 0u)
    {
        uint z = depth, id, frac;
        if (z < uFog.y)
        {
            id = 0u;
            frac = 0u;
        }
        else
        {
            z -= uFog.y;
            z = (z >> 2) << uFog.z;
            id = z >> 17;
            if (id >= 32u)
            {
                id = 32u;
                frac = 0u;
            }
            else
                frac = z & 0x1FFFFu;
        }
        uint density = (Density(id) * (0x20000u - frac) + Density(id + 1u) * frac) >> 17;
        if (density >= 127u) density = 128u;
        if (density != 0u)
        {
            uint fc = uFog.x;
            uvec3 fog = uvec3((fc << 3) & 0xF8u, (fc >> 2) & 0xF8u, (fc >> 7) & 0xF8u);
            if ((uFog.w & 2u) != 0u) rgb = (fog * density + rgb * (128u - density)) >> 7;
            a = (((fc >> 16) & 31u) * density + a * (128u - density)) >> 7;
        }
    }
    uvec3 c6 = rgb >> 2;
    oColor = a == 0u ? 0u : 0x40000000u | (a << 24) | (c6.b << 16) | (c6.g << 8) | c6.r;
}
