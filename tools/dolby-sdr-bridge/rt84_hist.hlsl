// Full 4:2:0 composer at each measured point; input planes are the original P010.
// Generated prefix supplies the same preset coefficients as the authored RPU.
Texture2D<float>  luma : register(t0);
Texture2D<float2> chroma : register(t1);
RWBuffer<uint> result : register(u0);
cbuffer Meta : register(b0) { uint width, height, top_left, unused; };
cbuffer Luma : register(b1) { float4 luma_lut[256]; };
groupshared uint histogram[4096];

uint y_code(int2 p) { return (uint)round(luma.Load(int3(p,0))*(65535.0/64.0)); }
uint y_filtered(int x,int y)
{
    return (y_code(int2(max(x-1,0),y)) + 2*y_code(int2(x,y)) +
            y_code(int2(min(x+1,(int)width-1),y)) + 2) >> 2;
}
float2 map_chroma(int x,int cy)
{
    float yy=((y_filtered(x,2*cy)+y_filtered(x,2*cy+1)+1)>>1)/1023.0;
    float2 uv=round(chroma.Load(int3(x/2,cy,0))*(65535.0/64.0))/1023.0;
    float u=uv.x,v=uv.y;
    float t[7]={yy,u,v,yy*u,yy*v,u*v,yy*u*v};
    float2 c=mmr_constant;
    [unroll] for(uint k=0;k<7;k++){
        float a=t[k],b=a*a;
        c+=mmr_linear[k]*a+mmr_squared[k]*b+mmr_cubic[k]*(b*a);
    }
    return saturate(c);
}
uint code_at(int2 p)
{
    // Use x = 4k, so the left-sited chroma is co-sited horizontally.
    int cy=p.y/2;
    float2 a=map_chroma(p.x,cy);
    if(!top_left)
        a=lerp(a,map_chroma(p.x,max(cy-1,0)),.25); // even y, vertically centred chroma
    a-=chroma_offset;
    uint y=y_code(p);
    float v=luma_lut[y>>2][y&3]+
        max(dot(row_r,a),max(dot(row_g,a),dot(row_b,a)));
    return (uint)round(clamp(v*4095.0,62.0,3079.0));
}
[numthreads(256,1,1)]
void main(uint local : SV_GroupIndex, uint3 id : SV_DispatchThreadID)
{
    for(uint j=local;j<4096;j+=256)histogram[j]=0;
    GroupMemoryBarrierWithGroupSync();
    uint cols=(width+3)/4,rows=(height+3)/4;
    for(uint i=id.x;i<cols*rows;i+=65536){
        uint q=code_at(int2((i%cols)*4,(i/cols)*4));
        InterlockedAdd(histogram[q],1);
    }
    GroupMemoryBarrierWithGroupSync();
    for(uint k=local;k<4096;k+=256){
        uint n=histogram[k];
        if(n) InterlockedAdd(result[k],n);
    }
}
