#if 0
//
// FX Version: fx_4_0
// Child effect (requires effect pool): false
//
// 1 local buffer(s)
//
cbuffer cb0
{
  float4  QuadDesc;           // Offset:  0, size:   16
  float4  TexCoords;          // Offset:   16, size:   16
  float4  TextColor;          // Offset:   32, size:   16
}

//
// 3 local object(s)
//
Texture2D tex;
BlendState bTextBlend
{
  AlphaToCoverageEnable = bool(FALSE /* 0 */);
  BlendEnable[0] = bool(TRUE /* 1 */);
  SrcBlend[0] = uint(SRC1_COLOR /* 16 */);
  DestBlend[0] = uint(INV_SRC1_COLOR /* 17 */);
  BlendOp[0] = uint(ADD /* 1 */);
  SrcBlendAlpha[0] = uint(SRC1_ALPHA /* 18 */);
  DestBlendAlpha[0] = uint(INV_SRC1_ALPHA /* 19 */);
  BlendOpAlpha[0] = uint(ADD /* 1 */);
  RenderTargetWriteMask[0] = byte(0x0f);
};
SamplerState sSampler
{
  Texture  = tex;
  AddressU = uint(CLAMP /* 3 */);
  AddressV = uint(CLAMP /* 3 */);
};

//
// 2 technique(s)
//
technique10 SampleTexture
{
  pass P0
  {
    VertexShader = asm {
      //
      // Generated by Microsoft (R) HLSL Shader Compiler 9.29.952.3111
      //
      //
      // Buffer Definitions: 
      //
      // cbuffer cb0
      // {
      //
      //   float4 QuadDesc;           // Offset:  0 Size:  16
      //   float4 TexCoords;          // Offset:   16 Size:  16
      //   float4 TextColor;          // Offset:   32 Size:  16 [unused]
      //
      // }
      //
      //
      // Resource Bindings:
      //
      // Name                 Type  Format     Dim Slot Elements
      // ------------------------------ ---------- ------- ----------- ---- --------
      // cb0                 cbuffer    NA      NA  0    1
      //
      //
      //
      // Input signature:
      //
      // Name         Index   Mask Register SysValue Format   Used
      // -------------------- ----- ------ -------- -------- ------ ------
      // POSITION         0   xyz     0   NONE  float   xy  
      //
      //
      // Output signature:
      //
      // Name         Index   Mask Register SysValue Format   Used
      // -------------------- ----- ------ -------- -------- ------ ------
      // SV_Position        0   xyzw    0    POS  float   xyzw
      // TEXCOORD         0   xy      1   NONE  float   xy  
      //
      //
      // Constant buffer to DX9 shader constant mappings:
      //
      // Target Reg Buffer  Start Reg # of Regs    Data Conversion
      // ---------- ------- --------- --------- ----------------------
      // c1     cb0       0     2  ( FLT, FLT, FLT, FLT)
      //
      //
      // Runtime generated constant mappings:
      //
      // Target Reg                 Constant Description
      // ---------- --------------------------------------------------
      // c0                Vertex Shader position offset
      //
      //
      // Level9 shader bytecode:
      //
        vs_2_x
        def c3, 0, 1, 0, 0
        dcl_texcoord v0
        mad oT0.xy, v0, c2.zwzw, c2
        mad r0.xy, v0, c1.zwzw, c1
        add oPos.xy, r0, c0
        mov oPos.zw, c3.xyxy
      
      // approximately 4 instruction slots used
      vs_4_0
      dcl_constantbuffer cb0[2], immediateIndexed
      dcl_input v0.xy
      dcl_output_siv o0.xyzw, position
      dcl_output o1.xy
      mad o0.xy, v0.xyxx, cb0[0].zwzz, cb0[0].xyxx
      mov o0.zw, l(0,0,0,1.000000)
      mad o1.xy, v0.xyxx, cb0[1].zwzz, cb0[1].xyxx
      ret 
      // Approximately 4 instruction slots used
          
    };
    GeometryShader = NULL;
    PixelShader = asm {
      //
      // Generated by Microsoft (R) HLSL Shader Compiler 9.29.952.3111
      //
      //
      // Resource Bindings:
      //
      // Name                 Type  Format     Dim Slot Elements
      // ------------------------------ ---------- ------- ----------- ---- --------
      // sSampler              sampler    NA      NA  0    1
      // tex                 texture  float4      2d  0    1
      //
      //
      //
      // Input signature:
      //
      // Name         Index   Mask Register SysValue Format   Used
      // -------------------- ----- ------ -------- -------- ------ ------
      // SV_Position        0   xyzw    0    POS  float     
      // TEXCOORD         0   xy      1   NONE  float   xy  
      //
      //
      // Output signature:
      //
      // Name         Index   Mask Register SysValue Format   Used
      // -------------------- ----- ------ -------- -------- ------ ------
      // SV_Target        0   xyzw    0   TARGET  float   xyzw
      //
      //
      // Sampler/Resource to DX9 shader sampler mappings:
      //
      // Target Sampler Source Sampler  Source Resource
      // -------------- --------------- ----------------
      // s0       s0        t0         
      //
      //
      // Level9 shader bytecode:
      //
        ps_2_x
        dcl t0.xy
        dcl_2d s0
        texld r0, t0, s0
        mov oC0, r0
      
      // approximately 2 instruction slots used (1 texture, 1 arithmetic)
      ps_4_0
      dcl_sampler s0, mode_default
      dcl_resource_texture2d (float,float,float,float) t0
      dcl_input_ps linear v1.xy
      dcl_output o0.xyzw
      sample o0.xyzw, v1.xyxx, t0.xyzw, s0
      ret 
      // Approximately 2 instruction slots used
          
    };
  }

}

technique10 SampleTextTexture
{
  pass P0
  {
    AB_BlendFactor = float4(0, 0, 0, 0);
    AB_SampleMask = uint(0xffffffff);
    BlendState = bTextBlend;
    VertexShader = asm {
      //
      // Generated by Microsoft (R) HLSL Shader Compiler 9.29.952.3111
      //
      //
      // Buffer Definitions: 
      //
      // cbuffer cb0
      // {
      //
      //   float4 QuadDesc;           // Offset:  0 Size:  16
      //   float4 TexCoords;          // Offset:   16 Size:  16
      //   float4 TextColor;          // Offset:   32 Size:  16 [unused]
      //
      // }
      //
      //
      // Resource Bindings:
      //
      // Name                 Type  Format     Dim Slot Elements
      // ------------------------------ ---------- ------- ----------- ---- --------
      // cb0                 cbuffer    NA      NA  0    1
      //
      //
      //
      // Input signature:
      //
      // Name         Index   Mask Register SysValue Format   Used
      // -------------------- ----- ------ -------- -------- ------ ------
      // POSITION         0   xyz     0   NONE  float   xy  
      //
      //
      // Output signature:
      //
      // Name         Index   Mask Register SysValue Format   Used
      // -------------------- ----- ------ -------- -------- ------ ------
      // SV_Position        0   xyzw    0    POS  float   xyzw
      // TEXCOORD         0   xy      1   NONE  float   xy  
      //
      //
      // Constant buffer to DX9 shader constant mappings:
      //
      // Target Reg Buffer  Start Reg # of Regs    Data Conversion
      // ---------- ------- --------- --------- ----------------------
      // c1     cb0       0     2  ( FLT, FLT, FLT, FLT)
      //
      //
      // Runtime generated constant mappings:
      //
      // Target Reg                 Constant Description
      // ---------- --------------------------------------------------
      // c0                Vertex Shader position offset
      //
      //
      // Level9 shader bytecode:
      //
        vs_2_x
        def c3, 0, 1, 0, 0
        dcl_texcoord v0
        mad oT0.xy, v0, c2.zwzw, c2
        mad r0.xy, v0, c1.zwzw, c1
        add oPos.xy, r0, c0
        mov oPos.zw, c3.xyxy
      
      // approximately 4 instruction slots used
      vs_4_0
      dcl_constantbuffer cb0[2], immediateIndexed
      dcl_input v0.xy
      dcl_output_siv o0.xyzw, position
      dcl_output o1.xy
      mad o0.xy, v0.xyxx, cb0[0].zwzz, cb0[0].xyxx
      mov o0.zw, l(0,0,0,1.000000)
      mad o1.xy, v0.xyxx, cb0[1].zwzz, cb0[1].xyxx
      ret 
      // Approximately 4 instruction slots used
          
    };
    GeometryShader = NULL;
    PixelShader = asm {
      //
      // Generated by Microsoft (R) HLSL Shader Compiler 9.29.952.3111
      //
      //
      // Buffer Definitions: 
      //
      // cbuffer cb0
      // {
      //
      //   float4 QuadDesc;           // Offset:  0 Size:  16 [unused]
      //   float4 TexCoords;          // Offset:   16 Size:  16 [unused]
      //   float4 TextColor;          // Offset:   32 Size:  16
      //
      // }
      //
      //
      // Resource Bindings:
      //
      // Name                 Type  Format     Dim Slot Elements
      // ------------------------------ ---------- ------- ----------- ---- --------
      // sSampler              sampler    NA      NA  0    1
      // tex                 texture  float4      2d  0    1
      // cb0                 cbuffer    NA      NA  0    1
      //
      //
      //
      // Input signature:
      //
      // Name         Index   Mask Register SysValue Format   Used
      // -------------------- ----- ------ -------- -------- ------ ------
      // SV_Position        0   xyzw    0    POS  float     
      // TEXCOORD         0   xy      1   NONE  float   xy  
      //
      //
      // Output signature:
      //
      // Name         Index   Mask Register SysValue Format   Used
      // -------------------- ----- ------ -------- -------- ------ ------
      // SV_Target        0   xyzw    0   TARGET  float   xyzw
      // SV_Target        1   xyzw    1   TARGET  float   xyzw
      //
      //
      // Constant buffer to DX9 shader constant mappings:
      //
      // Target Reg Buffer  Start Reg # of Regs    Data Conversion
      // ---------- ------- --------- --------- ----------------------
      // c0     cb0       2     1  ( FLT, FLT, FLT, FLT)
      //
      //
      // Sampler/Resource to DX9 shader sampler mappings:
      //
      // Target Sampler Source Sampler  Source Resource
      // -------------- --------------- ----------------
      // s0       s0        t0         
      //
      //
      // Level9 shader bytecode:
      //
        ps_2_x
        dcl t0.xy
        dcl_2d s0
        mov oC0, c0
        texld r0, t0, s0
        mul r0, r0.zyxy, c0.w
        mov oC1, r0
      
      // approximately 4 instruction slots used (1 texture, 3 arithmetic)
      ps_4_0
      dcl_constantbuffer cb0[3], immediateIndexed
      dcl_sampler s0, mode_default
      dcl_resource_texture2d (float,float,float,float) t0
      dcl_input_ps linear v1.xy
      dcl_output o0.xyzw
      dcl_output o1.xyzw
      dcl_temps 1
      mov o0.xyzw, cb0[2].xyzw
      sample r0.xyzw, v1.xyxx, t0.xyzw, s0
      mul o1.xyzw, r0.zyxy, cb0[2].wwww
      ret 
      // Approximately 4 instruction slots used
          
    };
  }

}

#endif

const BYTE g_main[] =
{
   68,  88,  66,  67,  53, 137, 
  246,  90,  48, 255, 136,  62, 
   98, 150, 163, 150, 147, 186, 
  203,  53,   1,   0,   0,   0, 
  225,  18,   0,   0,   1,   0, 
    0,   0,  36,   0,   0,   0, 
   70,  88,  49,  48, 181,  18, 
    0,   0,   1,  16, 255, 254, 
    1,   0,   0,   0,   3,   0, 
    0,   0,   3,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    2,   0,   0,   0,  57,  16, 
    0,   0,   0,   0,   0,   0, 
    1,   0,   0,   0,   0,   0, 
    0,   0,   1,   0,   0,   0, 
    0,   0,   0,   0,   1,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   4,   0, 
    0,   0,   4,   0,   0,   0, 
    0,   0,   0,   0,  99,  98, 
   48,   0, 102, 108, 111,  97, 
  116,  52,   0,   8,   0,   0, 
    0,   1,   0,   0,   0,   0, 
    0,   0,   0,  16,   0,   0, 
    0,  16,   0,   0,   0,  16, 
    0,   0,   0,  10,  33,   0, 
    0,  81, 117,  97, 100,  68, 
  101, 115,  99,   0,  84, 101, 
  120,  67, 111, 111, 114, 100, 
  115,   0,  84, 101, 120, 116, 
   67, 111, 108, 111, 114,   0, 
   84, 101, 120, 116, 117, 114, 
  101,  50,  68,   0,  72,   0, 
    0,   0,   2,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,  12,   0, 
    0,   0, 116, 101, 120,   0, 
   66, 108, 101, 110, 100,  83, 
  116,  97, 116, 101,   0, 114, 
    0,   0,   0,   2,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   2, 
    0,   0,   0,  98,  84, 101, 
  120, 116,  66, 108, 101, 110, 
  100,   0,   1,   0,   0,   0, 
    2,   0,   0,   0,   0,   0, 
    0,   0,   1,   0,   0,   0, 
    2,   0,   0,   0,   1,   0, 
    0,   0,   1,   0,   0,   0, 
    2,   0,   0,   0,  16,   0, 
    0,   0,   1,   0,   0,   0, 
    2,   0,   0,   0,  17,   0, 
    0,   0,   1,   0,   0,   0, 
    2,   0,   0,   0,   1,   0, 
    0,   0,   1,   0,   0,   0, 
    2,   0,   0,   0,  18,   0, 
    0,   0,   1,   0,   0,   0, 
    2,   0,   0,   0,  19,   0, 
    0,   0,   1,   0,   0,   0, 
    2,   0,   0,   0,   1,   0, 
    0,   0,   1,   0,   0,   0, 
    3,   0,   0,   0,  15,   0, 
    0,   0,  83,  97, 109, 112, 
  108, 101, 114,  83, 116,  97, 
  116, 101,   0,  16,   1,   0, 
    0,   2,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,  21,   0,   0, 
    0, 115,  83,  97, 109, 112, 
  108, 101, 114,   0,   1,   0, 
    0,   0,   2,   0,   0,   0, 
    3,   0,   0,   0,   1,   0, 
    0,   0,   2,   0,   0,   0, 
    3,   0,   0,   0,  83,  97, 
  109, 112, 108, 101,  84, 101, 
  120, 116, 117, 114, 101,   0, 
   80,  48,   0, 188,   3,   0, 
    0,  68,  88,  66,  67, 211, 
   96, 210, 105,  17, 130,  48, 
  194, 178, 234,  96, 122, 215, 
  146, 217, 132,   1,   0,   0, 
    0, 188,   3,   0,   0,   6, 
    0,   0,   0,  56,   0,   0, 
    0, 228,   0,   0,   0, 168, 
    1,   0,   0,  36,   2,   0, 
    0,  48,   3,   0,   0, 100, 
    3,   0,   0,  65, 111, 110, 
   57, 164,   0,   0,   0, 164, 
    0,   0,   0,   0,   2, 254, 
  255, 112,   0,   0,   0,  52, 
    0,   0,   0,   1,   0,  36, 
    0,   0,   0,  48,   0,   0, 
    0,  48,   0,   0,   0,  36, 
    0,   1,   0,  48,   0,   0, 
    0,   0,   0,   2,   0,   1, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   1,   2, 254, 
  255,  81,   0,   0,   5,   3, 
    0,  15, 160,   0,   0,   0, 
    0,   0,   0, 128,  63,   0, 
    0,   0,   0,   0,   0,   0, 
    0,  31,   0,   0,   2,   5, 
    0,   0, 128,   0,   0,  15, 
  144,   4,   0,   0,   4,   0, 
    0,   3, 224,   0,   0, 228, 
  144,   2,   0, 238, 160,   2, 
    0, 228, 160,   4,   0,   0, 
    4,   0,   0,   3, 128,   0, 
    0, 228, 144,   1,   0, 238, 
  160,   1,   0, 228, 160,   2, 
    0,   0,   3,   0,   0,   3, 
  192,   0,   0, 228, 128,   0, 
    0, 228, 160,   1,   0,   0, 
    2,   0,   0,  12, 192,   3, 
    0,  68, 160, 255, 255,   0, 
    0,  83,  72,  68,  82, 188, 
    0,   0,   0,  64,   0,   1, 
    0,  47,   0,   0,   0,  89, 
    0,   0,   4,  70, 142,  32, 
    0,   0,   0,   0,   0,   2, 
    0,   0,   0,  95,   0,   0, 
    3,  50,  16,  16,   0,   0, 
    0,   0,   0, 103,   0,   0, 
    4, 242,  32,  16,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0, 101,   0,   0,   3,  50, 
   32,  16,   0,   1,   0,   0, 
    0,  50,   0,   0,  11,  50, 
   32,  16,   0,   0,   0,   0, 
    0,  70,  16,  16,   0,   0, 
    0,   0,   0, 230, 138,  32, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,  70, 128,  32, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,  54,   0,   0, 
    8, 194,  32,  16,   0,   0, 
    0,   0,   0,   2,  64,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0, 128,  63,  50, 
    0,   0,  11,  50,  32,  16, 
    0,   1,   0,   0,   0,  70, 
   16,  16,   0,   0,   0,   0, 
    0, 230, 138,  32,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,  70, 128,  32,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,  62,   0,   0,   1,  83, 
   84,  65,  84, 116,   0,   0, 
    0,   4,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   3,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,  82,  68,  69, 
   70,   4,   1,   0,   0,   1, 
    0,   0,   0,  64,   0,   0, 
    0,   1,   0,   0,   0,  28, 
    0,   0,   0,   0,   4, 254, 
  255,   0,   1,   0,   0, 208, 
    0,   0,   0,  60,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,   0,   0,   0,   0,  99, 
   98,  48,   0,  60,   0,   0, 
    0,   3,   0,   0,   0,  88, 
    0,   0,   0,  48,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0, 160,   0,   0, 
    0,   0,   0,   0,   0,  16, 
    0,   0,   0,   2,   0,   0, 
    0, 172,   0,   0,   0,   0, 
    0,   0,   0, 188,   0,   0, 
    0,  16,   0,   0,   0,  16, 
    0,   0,   0,   2,   0,   0, 
    0, 172,   0,   0,   0,   0, 
    0,   0,   0, 198,   0,   0, 
    0,  32,   0,   0,   0,  16, 
    0,   0,   0,   0,   0,   0, 
    0, 172,   0,   0,   0,   0, 
    0,   0,   0,  81, 117,  97, 
  100,  68, 101, 115,  99,   0, 
  171, 171, 171,   1,   0,   3, 
    0,   1,   0,   4,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,  84, 101, 120,  67, 111, 
  111, 114, 100, 115,   0,  84, 
  101, 120, 116,  67, 111, 108, 
  111, 114,   0,  77, 105,  99, 
  114, 111, 115, 111, 102, 116, 
   32,  40,  82,  41,  32,  72, 
   76,  83,  76,  32,  83, 104, 
   97, 100, 101, 114,  32,  67, 
  111, 109, 112, 105, 108, 101, 
  114,  32,  57,  46,  50,  57, 
   46,  57,  53,  50,  46,  51, 
   49,  49,  49,   0, 171, 171, 
  171,  73,  83,  71,  78,  44, 
    0,   0,   0,   1,   0,   0, 
    0,   8,   0,   0,   0,  32, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   3, 
    0,   0,   0,   0,   0,   0, 
    0,   7,   3,   0,   0,  80, 
   79,  83,  73,  84,  73,  79, 
   78,   0, 171, 171, 171,  79, 
   83,  71,  78,  80,   0,   0, 
    0,   2,   0,   0,   0,   8, 
    0,   0,   0,  56,   0,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0,   3,   0,   0, 
    0,   0,   0,   0,   0,  15, 
    0,   0,   0,  68,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   3,   0,   0, 
    0,   1,   0,   0,   0,   3, 
   12,   0,   0,  83,  86,  95, 
   80, 111, 115, 105, 116, 105, 
  111, 110,   0,  84,  69,  88, 
   67,  79,  79,  82,  68,   0, 
  171, 171, 171, 107,   1,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0,   2,   0,   0, 
    0,   0,   0,   0,   0, 188, 
    2,   0,   0,  68,  88,  66, 
   67,  57, 173, 135,  37,   0, 
   15, 237,  50, 142,  80,  59, 
  160,  81, 240,  60, 171,   1, 
    0,   0,   0, 188,   2,   0, 
    0,   6,   0,   0,   0,  56, 
    0,   0,   0, 164,   0,   0, 
    0,  16,   1,   0,   0, 140, 
    1,   0,   0,  48,   2,   0, 
    0, 136,   2,   0,   0,  65, 
  111, 110,  57, 100,   0,   0, 
    0, 100,   0,   0,   0,   0, 
    2, 255, 255,  60,   0,   0, 
    0,  40,   0,   0,   0,   0, 
    0,  40,   0,   0,   0,  40, 
    0,   0,   0,  40,   0,   1, 
    0,  36,   0,   0,   0,  40, 
    0,   0,   0,   0,   0,   1, 
    2, 255, 255,  31,   0,   0, 
    2,   0,   0,   0, 128,   0, 
    0,   3, 176,  31,   0,   0, 
    2,   0,   0,   0, 144,   0, 
    8,  15, 160,  66,   0,   0, 
    3,   0,   0,  15, 128,   0, 
    0, 228, 176,   0,   8, 228, 
  160,   1,   0,   0,   2,   0, 
    8,  15, 128,   0,   0, 228, 
  128, 255, 255,   0,   0,  83, 
   72,  68,  82, 100,   0,   0, 
    0,  64,   0,   0,   0,  25, 
    0,   0,   0,  90,   0,   0, 
    3,   0,  96,  16,   0,   0, 
    0,   0,   0,  88,  24,   0, 
    4,   0, 112,  16,   0,   0, 
    0,   0,   0,  85,  85,   0, 
    0,  98,  16,   0,   3,  50, 
   16,  16,   0,   1,   0,   0, 
    0, 101,   0,   0,   3, 242, 
   32,  16,   0,   0,   0,   0, 
    0,  69,   0,   0,   9, 242, 
   32,  16,   0,   0,   0,   0, 
    0,  70,  16,  16,   0,   1, 
    0,   0,   0,  70, 126,  16, 
    0,   0,   0,   0,   0,   0, 
   96,  16,   0,   0,   0,   0, 
    0,  62,   0,   0,   1,  83, 
   84,  65,  84, 116,   0,   0, 
    0,   2,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   2,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,  82,  68,  69, 
   70, 156,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   2,   0,   0,   0,  28, 
    0,   0,   0,   0,   4, 255, 
  255,   0,   1,   0,   0, 105, 
    0,   0,   0,  92,   0,   0, 
    0,   3,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,   0,   0,   0,   0, 101, 
    0,   0,   0,   2,   0,   0, 
    0,   5,   0,   0,   0,   4, 
    0,   0,   0, 255, 255, 255, 
  255,   0,   0,   0,   0,   1, 
    0,   0,   0,  12,   0,   0, 
    0, 115,  83,  97, 109, 112, 
  108, 101, 114,   0, 116, 101, 
  120,   0,  77, 105,  99, 114, 
  111, 115, 111, 102, 116,  32, 
   40,  82,  41,  32,  72,  76, 
   83,  76,  32,  83, 104,  97, 
  100, 101, 114,  32,  67, 111, 
  109, 112, 105, 108, 101, 114, 
   32,  57,  46,  50,  57,  46, 
   57,  53,  50,  46,  51,  49, 
   49,  49,   0, 171, 171,  73, 
   83,  71,  78,  80,   0,   0, 
    0,   2,   0,   0,   0,   8, 
    0,   0,   0,  56,   0,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0,   3,   0,   0, 
    0,   0,   0,   0,   0,  15, 
    0,   0,   0,  68,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   3,   0,   0, 
    0,   1,   0,   0,   0,   3, 
    3,   0,   0,  83,  86,  95, 
   80, 111, 115, 105, 116, 105, 
  111, 110,   0,  84,  69,  88, 
   67,  79,  79,  82,  68,   0, 
  171, 171, 171,  79,  83,  71, 
   78,  44,   0,   0,   0,   1, 
    0,   0,   0,   8,   0,   0, 
    0,  32,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   3,   0,   0,   0,   0, 
    0,   0,   0,  15,   0,   0, 
    0,  83,  86,  95,  84,  97, 
  114, 103, 101, 116,   0, 171, 
  171,  63,   5,   0,   0,   0, 
    0,   0,   0,  83,  97, 109, 
  112, 108, 101,  84, 101, 120, 
  116,  84, 101, 120, 116, 117, 
  114, 101,   0,   4,   0,   0, 
    0,   1,   0,   0,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0,   0,   0,   0, 
    0,   1,   0,   0,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,   3,   0,   0,   0, 255, 
  255, 255, 255, 188,   3,   0, 
    0,  68,  88,  66,  67, 211, 
   96, 210, 105,  17, 130,  48, 
  194, 178, 234,  96, 122, 215, 
  146, 217, 132,   1,   0,   0, 
    0, 188,   3,   0,   0,   6, 
    0,   0,   0,  56,   0,   0, 
    0, 228,   0,   0,   0, 168, 
    1,   0,   0,  36,   2,   0, 
    0,  48,   3,   0,   0, 100, 
    3,   0,   0,  65, 111, 110, 
   57, 164,   0,   0,   0, 164, 
    0,   0,   0,   0,   2, 254, 
  255, 112,   0,   0,   0,  52, 
    0,   0,   0,   1,   0,  36, 
    0,   0,   0,  48,   0,   0, 
    0,  48,   0,   0,   0,  36, 
    0,   1,   0,  48,   0,   0, 
    0,   0,   0,   2,   0,   1, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   1,   2, 254, 
  255,  81,   0,   0,   5,   3, 
    0,  15, 160,   0,   0,   0, 
    0,   0,   0, 128,  63,   0, 
    0,   0,   0,   0,   0,   0, 
    0,  31,   0,   0,   2,   5, 
    0,   0, 128,   0,   0,  15, 
  144,   4,   0,   0,   4,   0, 
    0,   3, 224,   0,   0, 228, 
  144,   2,   0, 238, 160,   2, 
    0, 228, 160,   4,   0,   0, 
    4,   0,   0,   3, 128,   0, 
    0, 228, 144,   1,   0, 238, 
  160,   1,   0, 228, 160,   2, 
    0,   0,   3,   0,   0,   3, 
  192,   0,   0, 228, 128,   0, 
    0, 228, 160,   1,   0,   0, 
    2,   0,   0,  12, 192,   3, 
    0,  68, 160, 255, 255,   0, 
    0,  83,  72,  68,  82, 188, 
    0,   0,   0,  64,   0,   1, 
    0,  47,   0,   0,   0,  89, 
    0,   0,   4,  70, 142,  32, 
    0,   0,   0,   0,   0,   2, 
    0,   0,   0,  95,   0,   0, 
    3,  50,  16,  16,   0,   0, 
    0,   0,   0, 103,   0,   0, 
    4, 242,  32,  16,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0, 101,   0,   0,   3,  50, 
   32,  16,   0,   1,   0,   0, 
    0,  50,   0,   0,  11,  50, 
   32,  16,   0,   0,   0,   0, 
    0,  70,  16,  16,   0,   0, 
    0,   0,   0, 230, 138,  32, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,  70, 128,  32, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,  54,   0,   0, 
    8, 194,  32,  16,   0,   0, 
    0,   0,   0,   2,  64,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0, 128,  63,  50, 
    0,   0,  11,  50,  32,  16, 
    0,   1,   0,   0,   0,  70, 
   16,  16,   0,   0,   0,   0, 
    0, 230, 138,  32,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,  70, 128,  32,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,  62,   0,   0,   1,  83, 
   84,  65,  84, 116,   0,   0, 
    0,   4,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   3,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,  82,  68,  69, 
   70,   4,   1,   0,   0,   1, 
    0,   0,   0,  64,   0,   0, 
    0,   1,   0,   0,   0,  28, 
    0,   0,   0,   0,   4, 254, 
  255,   0,   1,   0,   0, 208, 
    0,   0,   0,  60,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,   0,   0,   0,   0,  99, 
   98,  48,   0,  60,   0,   0, 
    0,   3,   0,   0,   0,  88, 
    0,   0,   0,  48,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0, 160,   0,   0, 
    0,   0,   0,   0,   0,  16, 
    0,   0,   0,   2,   0,   0, 
    0, 172,   0,   0,   0,   0, 
    0,   0,   0, 188,   0,   0, 
    0,  16,   0,   0,   0,  16, 
    0,   0,   0,   2,   0,   0, 
    0, 172,   0,   0,   0,   0, 
    0,   0,   0, 198,   0,   0, 
    0,  32,   0,   0,   0,  16, 
    0,   0,   0,   0,   0,   0, 
    0, 172,   0,   0,   0,   0, 
    0,   0,   0,  81, 117,  97, 
  100,  68, 101, 115,  99,   0, 
  171, 171, 171,   1,   0,   3, 
    0,   1,   0,   4,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,  84, 101, 120,  67, 111, 
  111, 114, 100, 115,   0,  84, 
  101, 120, 116,  67, 111, 108, 
  111, 114,   0,  77, 105,  99, 
  114, 111, 115, 111, 102, 116, 
   32,  40,  82,  41,  32,  72, 
   76,  83,  76,  32,  83, 104, 
   97, 100, 101, 114,  32,  67, 
  111, 109, 112, 105, 108, 101, 
  114,  32,  57,  46,  50,  57, 
   46,  57,  53,  50,  46,  51, 
   49,  49,  49,   0, 171, 171, 
  171,  73,  83,  71,  78,  44, 
    0,   0,   0,   1,   0,   0, 
    0,   8,   0,   0,   0,  32, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   3, 
    0,   0,   0,   0,   0,   0, 
    0,   7,   3,   0,   0,  80, 
   79,  83,  73,  84,  73,  79, 
   78,   0, 171, 171, 171,  79, 
   83,  71,  78,  80,   0,   0, 
    0,   2,   0,   0,   0,   8, 
    0,   0,   0,  56,   0,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0,   3,   0,   0, 
    0,   0,   0,   0,   0,  15, 
    0,   0,   0,  68,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   3,   0,   0, 
    0,   1,   0,   0,   0,   3, 
   12,   0,   0,  83,  86,  95, 
   80, 111, 115, 105, 116, 105, 
  111, 110,   0,  84,  69,  88, 
   67,  79,  79,  82,  68,   0, 
  171, 171, 171,  73,   8,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0,   2,   0,   0, 
    0,   0,   0,   0,   0,  16, 
    4,   0,   0,  68,  88,  66, 
   67, 175, 129, 196, 242, 129, 
   85, 126,  90, 106, 179,  87, 
   12, 194,   2, 170, 102,   1, 
    0,   0,   0,  16,   4,   0, 
    0,   6,   0,   0,   0,  56, 
    0,   0,   0, 204,   0,   0, 
    0, 148,   1,   0,   0,  16, 
    2,   0,   0, 108,   3,   0, 
    0, 196,   3,   0,   0,  65, 
  111, 110,  57, 140,   0,   0, 
    0, 140,   0,   0,   0,   0, 
    2, 255, 255,  88,   0,   0, 
    0,  52,   0,   0,   0,   1, 
    0,  40,   0,   0,   0,  52, 
    0,   0,   0,  52,   0,   1, 
    0,  36,   0,   0,   0,  52, 
    0,   0,   0,   0,   0,   0, 
    0,   2,   0,   1,   0,   0, 
    0,   0,   0,   0,   0,   1, 
    2, 255, 255,  31,   0,   0, 
    2,   0,   0,   0, 128,   0, 
    0,   3, 176,  31,   0,   0, 
    2,   0,   0,   0, 144,   0, 
    8,  15, 160,   1,   0,   0, 
    2,   0,   8,  15, 128,   0, 
    0, 228, 160,  66,   0,   0, 
    3,   0,   0,  15, 128,   0, 
    0, 228, 176,   0,   8, 228, 
  160,   5,   0,   0,   3,   0, 
    0,  15, 128,   0,   0,  70, 
  128,   0,   0, 255, 160,   1, 
    0,   0,   2,   1,   8,  15, 
  128,   0,   0, 228, 128, 255, 
  255,   0,   0,  83,  72,  68, 
   82, 192,   0,   0,   0,  64, 
    0,   0,   0,  48,   0,   0, 
    0,  89,   0,   0,   4,  70, 
  142,  32,   0,   0,   0,   0, 
    0,   3,   0,   0,   0,  90, 
    0,   0,   3,   0,  96,  16, 
    0,   0,   0,   0,   0,  88, 
   24,   0,   4,   0, 112,  16, 
    0,   0,   0,   0,   0,  85, 
   85,   0,   0,  98,  16,   0, 
    3,  50,  16,  16,   0,   1, 
    0,   0,   0, 101,   0,   0, 
    3, 242,  32,  16,   0,   0, 
    0,   0,   0, 101,   0,   0, 
    3, 242,  32,  16,   0,   1, 
    0,   0,   0, 104,   0,   0, 
    2,   1,   0,   0,   0,  54, 
    0,   0,   6, 242,  32,  16, 
    0,   0,   0,   0,   0,  70, 
  142,  32,   0,   0,   0,   0, 
    0,   2,   0,   0,   0,  69, 
    0,   0,   9, 242,   0,  16, 
    0,   0,   0,   0,   0,  70, 
   16,  16,   0,   1,   0,   0, 
    0,  70, 126,  16,   0,   0, 
    0,   0,   0,   0,  96,  16, 
    0,   0,   0,   0,   0,  56, 
    0,   0,   8, 242,  32,  16, 
    0,   1,   0,   0,   0, 102, 
    4,  16,   0,   0,   0,   0, 
    0, 246, 143,  32,   0,   0, 
    0,   0,   0,   2,   0,   0, 
    0,  62,   0,   0,   1,  83, 
   84,  65,  84, 116,   0,   0, 
    0,   4,   0,   0,   0,   1, 
    0,   0,   0,   0,   0,   0, 
    0,   3,   0,   0,   0,   1, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,  82,  68,  69, 
   70,  84,   1,   0,   0,   1, 
    0,   0,   0, 144,   0,   0, 
    0,   3,   0,   0,   0,  28, 
    0,   0,   0,   0,   4, 255, 
  255,   0,   1,   0,   0,  32, 
    1,   0,   0, 124,   0,   0, 
    0,   3,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,   0,   0,   0,   0, 133, 
    0,   0,   0,   2,   0,   0, 
    0,   5,   0,   0,   0,   4, 
    0,   0,   0, 255, 255, 255, 
  255,   0,   0,   0,   0,   1, 
    0,   0,   0,  12,   0,   0, 
    0, 137,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   1,   0,   0,   0,   0, 
    0,   0,   0, 115,  83,  97, 
  109, 112, 108, 101, 114,   0, 
  116, 101, 120,   0,  99,  98, 
   48,   0, 171, 171, 171, 137, 
    0,   0,   0,   3,   0,   0, 
    0, 168,   0,   0,   0,  48, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0, 240, 
    0,   0,   0,   0,   0,   0, 
    0,  16,   0,   0,   0,   0, 
    0,   0,   0, 252,   0,   0, 
    0,   0,   0,   0,   0,  12, 
    1,   0,   0,  16,   0,   0, 
    0,  16,   0,   0,   0,   0, 
    0,   0,   0, 252,   0,   0, 
    0,   0,   0,   0,   0,  22, 
    1,   0,   0,  32,   0,   0, 
    0,  16,   0,   0,   0,   2, 
    0,   0,   0, 252,   0,   0, 
    0,   0,   0,   0,   0,  81, 
  117,  97, 100,  68, 101, 115, 
   99,   0, 171, 171, 171,   1, 
    0,   3,   0,   1,   0,   4, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,  84, 101, 120, 
   67, 111, 111, 114, 100, 115, 
    0,  84, 101, 120, 116,  67, 
  111, 108, 111, 114,   0,  77, 
  105,  99, 114, 111, 115, 111, 
  102, 116,  32,  40,  82,  41, 
   32,  72,  76,  83,  76,  32, 
   83, 104,  97, 100, 101, 114, 
   32,  67, 111, 109, 112, 105, 
  108, 101, 114,  32,  57,  46, 
   50,  57,  46,  57,  53,  50, 
   46,  51,  49,  49,  49,   0, 
  171, 171, 171,  73,  83,  71, 
   78,  80,   0,   0,   0,   2, 
    0,   0,   0,   8,   0,   0, 
    0,  56,   0,   0,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,   3,   0,   0,   0,   0, 
    0,   0,   0,  15,   0,   0, 
    0,  68,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   3,   0,   0,   0,   1, 
    0,   0,   0,   3,   3,   0, 
    0,  83,  86,  95,  80, 111, 
  115, 105, 116, 105, 111, 110, 
    0,  84,  69,  88,  67,  79, 
   79,  82,  68,   0, 171, 171, 
  171,  79,  83,  71,  78,  68, 
    0,   0,   0,   2,   0,   0, 
    0,   8,   0,   0,   0,  56, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   3, 
    0,   0,   0,   0,   0,   0, 
    0,  15,   0,   0,   0,  56, 
    0,   0,   0,   1,   0,   0, 
    0,   0,   0,   0,   0,   3, 
    0,   0,   0,   1,   0,   0, 
    0,  15,   0,   0,   0,  83, 
   86,  95,  84,  97, 114, 103, 
  101, 116,   0, 171, 171,  29, 
   12,   0,   0,   0,   0,   0, 
    0,   4,   0,   0,   0,  48, 
    0,   0,   0,   0,   0,   0, 
    0,   3,   0,   0,   0, 255, 
  255, 255, 255,   0,   0,   0, 
    0,  43,   0,   0,   0,  15, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,  52, 
    0,   0,   0,  15,   0,   0, 
    0,   0,   0,   0,   0,  16, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,  62,   0,   0, 
    0,  15,   0,   0,   0,   0, 
    0,   0,   0,  32,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0,   0,   0,   0,   0,   0, 
    0, 110,   0,   0,   0,  82, 
    0,   0,   0,   0,   0,   0, 
    0, 255, 255, 255, 255,   0, 
    0,   0,   0, 153,   0,   0, 
    0, 125,   0,   0,   0,   0, 
    0,   0,   0, 255, 255, 255, 
  255,   9,   0,   0,   0,  36, 
    0,   0,   0,   0,   0,   0, 
    0,   1,   0,   0,   0, 164, 
    0,   0,   0,  37,   0,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0, 176,   0,   0, 
    0,  38,   0,   0,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0, 188,   0,   0,   0,  39, 
    0,   0,   0,   0,   0,   0, 
    0,   1,   0,   0,   0, 200, 
    0,   0,   0,  40,   0,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0, 212,   0,   0, 
    0,  41,   0,   0,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0, 224,   0,   0,   0,  42, 
    0,   0,   0,   0,   0,   0, 
    0,   1,   0,   0,   0, 236, 
    0,   0,   0,  43,   0,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0, 248,   0,   0, 
    0,  44,   0,   0,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,   4,   1,   0,   0,   0, 
    0,   0,   0,  57,   1,   0, 
    0,  29,   1,   0,   0,   0, 
    0,   0,   0, 255, 255, 255, 
  255,   3,   0,   0,   0,  55, 
    0,   0,   0,   0,   0,   0, 
    0,   2,   0,   0,   0, 110, 
    0,   0,   0,  46,   0,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0,  66,   1,   0, 
    0,  47,   0,   0,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,  78,   1,   0,   0,   0, 
    0,   0,   0,  90,   1,   0, 
    0,   1,   0,   0,   0,   0, 
    0,   0,   0, 104,   1,   0, 
    0,   3,   0,   0,   0,   0, 
    0,   0,   0,   6,   0,   0, 
    0,   0,   0,   0,   0,   7, 
    0,   0,   0,  43,   5,   0, 
    0,   8,   0,   0,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,  51,   5,   0,   0,   7, 
    0,   0,   0,   0,   0,   0, 
    0,   7,   0,   0,   0, 255, 
    7,   0,   0,   7,   8,   0, 
    0,   1,   0,   0,   0,   0, 
    0,   0,   0, 104,   1,   0, 
    0,   6,   0,   0,   0,   0, 
    0,   0,   0,  10,   0,   0, 
    0,   0,   0,   0,   0,   1, 
    0,   0,   0,  25,   8,   0, 
    0,  11,   0,   0,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,  61,   8,   0,   0,   2, 
    0,   0,   0,   0,   0,   0, 
    0,   2,   0,   0,   0, 153, 
    0,   0,   0,   6,   0,   0, 
    0,   0,   0,   0,   0,   7, 
    0,   0,   0,   9,  12,   0, 
    0,   8,   0,   0,   0,   0, 
    0,   0,   0,   1,   0,   0, 
    0,  17,  12,   0,   0,   7, 
    0,   0,   0,   0,   0,   0, 
    0,   7,   0,   0,   0,  49, 
   16,   0,   0
};
