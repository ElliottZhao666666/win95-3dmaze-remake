cbuffer Frame : register(b0) {
    matrix viewProj;
    float wallHeight;
    float elapsed;
    float2 padding0;
    float3 eye;
    float padding1;
};

struct VSIn { float3 pos : POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; float material : TEXCOORD1; float4 color : COLOR0; };
struct VSOut { float4 pos : SV_POSITION; float3 normal : NORMAL; float2 uv : TEXCOORD0; float material : TEXCOORD1; float fog : TEXCOORD2; float4 color : COLOR0; };

VSOut VSMain(VSIn v) {
    VSOut o;
    float y = v.pos.y;
    if (v.material < 0.5 || (v.material > 2.5 && v.material < 3.5) || v.material > 8.5)
        y = v.pos.y + (wallHeight - 1.0) * 0.82;
    float3 p = float3(v.pos.x, y, v.pos.z);
    o.pos = mul(float4(p, 1.0), viewProj);
    o.normal = v.normal;
    o.uv = v.uv;
    o.material = v.material;
    o.color = v.color;
    o.fog = saturate(length(p - eye) / 42.0);
    return o;
}

Texture2D wallTex : register(t0);
Texture2D floorTex : register(t1);
Texture2D ceilingTex : register(t2);
Texture2D picTex : register(t3);
Texture2D ratTex : register(t4);
Texture2D finTex : register(t5);
Texture2D startTex : register(t6);
Texture2D glTex : register(t7);
Texture2D dxTex : register(t8);
Texture2D ybTex : register(t9);
SamplerState texSampler : register(s0);

float4 PSMain(VSOut i) : SV_TARGET {
    float4 c;
    if (i.material < 0.5) c = wallTex.Sample(texSampler, i.uv);
    else if (i.material < 1.5) c = floorTex.Sample(texSampler, i.uv);
    else if (i.material < 2.5) c = ceilingTex.Sample(texSampler, i.uv);
    else if (i.material < 3.5) c = picTex.Sample(texSampler, i.uv);
    else if (i.material < 4.5) c = ratTex.Sample(texSampler, i.uv);
    else if (i.material < 5.5) c = finTex.Sample(texSampler, i.uv);
    else if (i.material < 6.5) c = startTex.Sample(texSampler, i.uv);
    else if (i.material < 7.5) c = glTex.Sample(texSampler, i.uv);
    else if (i.material > 8.5 && i.material < 9.5) c = dxTex.Sample(texSampler, i.uv);
    else if (i.material > 9.5 && i.material < 10.5) c = ybTex.Sample(texSampler, i.uv);
    else c = i.color;
    if (i.material >= 4.0 && i.material < 8.0 && c.a < 0.01) discard;
    float3 n = normalize(i.normal);
    float light = 0.70 + 0.30 * saturate(dot(n, normalize(float3(-0.4, 0.8, -0.3))));
    float ao = 1.0 - 0.20 * saturate(i.fog);
    c.rgb *= light * ao;
    c.rgb = lerp(c.rgb, float3(0.015, 0.018, 0.025), i.fog * 0.42);
    return c;
}

struct MapIn { float2 pos : POSITION; float4 color : COLOR0; };
struct MapOut { float4 pos : SV_POSITION; float4 color : COLOR0; };
MapOut MapVS(MapIn v) { MapOut o; o.pos = float4(v.pos, 0.0, 1.0); o.color = v.color; return o; }
float4 MapPS(MapOut i) : SV_TARGET { return i.color; }
