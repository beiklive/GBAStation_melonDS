#version 460
layout(local_size_x=8,local_size_y=8) in;
layout(binding=0, std430) readonly buffer Source {
    uint scale, worldReady;
    uint native[98304]; // B,G,R and a byte indicating whether this pixel may use enhanced 3D
    uint world[];
} src;
layout(binding=0, rgba8) writeonly uniform image2D topScreen;
layout(binding=1, rgba8) writeonly uniform image2D bottomScreen;
void main() {
    uvec2 p=gl_GlobalInvocationID.xy;
    uint width=src.scale*256u;
    uint pos=(p.y/src.scale)*256u+p.x/src.scale;
    for(uint s=0; s<2; s++) {
        uint px=src.native[s*49152u+pos];
        if(src.worldReady!=0u && (px>>24)!=0u) {
            uint w=src.world[p.y*width+p.x];
            if((w>>24)==255u) px=w;
        }
        vec4 color=vec4(float((px>>16)&255u),float((px>>8)&255u),float(px&255u),255.)/255.;
        if(s==0u) imageStore(topScreen,ivec2(p),color); else imageStore(bottomScreen,ivec2(p),color);
    }
}
