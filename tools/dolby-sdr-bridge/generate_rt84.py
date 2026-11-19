from pathlib import Path
from itertools import accumulate
import ctypes as ct
import json
D=Path(__file__).resolve().parent

def compile_cs(source):
    """Compile D3D11 bytecode at build time, not on the first frame."""
    compiler=ct.WinDLL("d3dcompiler_47.dll",winmode=0x800)
    f=compiler.D3DCompile
    v=ct.c_void_p;s=ct.c_char_p
    f.argtypes=[s,ct.c_size_t,s,v,v,s,s,ct.c_uint,ct.c_uint,ct.POINTER(v),ct.POINTER(v)]
    f.restype=ct.c_long
    def method(blob,index,typ):
        table=ct.cast(blob,ct.POINTER(ct.POINTER(v))).contents
        return ct.WINFUNCTYPE(typ,v)(table[index])(blob)
    def bytes_of(blob):
        return ct.string_at(method(blob,3,v),method(blob,4,ct.c_size_t))
    code,err=v(),v()
    source=source.encode("utf8")
    status=f(source,len(source),b"rt84_hist",None,None,b"main",b"cs_5_0",1<<15,0,ct.byref(code),ct.byref(err))
    try:
        if status<0:
            raise RuntimeError(bytes_of(err).decode("utf8",errors="replace") if err else f"shader {status:#x}")
        result=bytes_of(code)
        assert result.startswith(b"DXBC")
        return result
    finally:
        for blob in (code,err):
            if blob:method(blob,2,ct.c_ulong)


def program():
    """Derive only content-independent preset coefficients from the decoded offline RPU."""
    source=D/"hlg-analysis-preset.json"
    r=json.loads(source.read_text(encoding="utf8"))[0]
    h=r["header"];dm=r["vdr_dm_data"];curves=r["rpu_data_mapping"]["curves"]
    assert r["dovi_profile"]==8 and h["coefficient_log2_denom"]==23
    assert (dm["source_min_pq"],dm["source_max_pq"])==(62,3079)
    fixed=lambda a,b:a+b/2**23
    matrix=[dm[f"ycc_to_rgb_coef{k}"]/8192 for k in range(9)]
    offset=[dm[f"ycc_to_rgb_offset{k}"]/2**28 for k in range(3)]
    assert matrix[0]==matrix[3]==matrix[6]
    assert curves[0]["poly_order_minus1"]==[1]*8
    edges=list(accumulate(curves[0]["pivots"]))
    coefs=[[fixed(a,b) for a,b in zip(i,j)] for i,j in zip(curves[0]["poly_coef_int"],curves[0]["poly_coef"])]
    lut=[]
    for i in range(1024):
        t=i/1023
        seg=max([j for j,e in enumerate(edges[:-1]) if i>=e],default=0)
        q=sum(x*t**j for j,x in enumerate(coefs[seg]))
        lut.append((min(max(q,edges[0]/1023),edges[-1]/1023)-offset[0])*matrix[0])
    assert round(lut[721]*4095)==2389
    uv=curves[1:]
    assert all(c["mmr_order_minus1"]==[2] and c["pivots"]==[0,1023] for c in uv)
    mmr=[[[fixed(a,b) for a,b in zip(x,y)] for x,y in zip(c["mmr_coef_int"][0],c["mmr_coef"][0])] for c in uv]
    constant=[fixed(c["mmr_constant_int"][0],c["mmr_constant"][0]) for c in uv]
    terms=[721/1023,512/1023,512/1023]
    y,u,v=terms; terms += [y*u,y*v,u*v,y*u*v]
    delta=[c+sum(z*x**(p+1) for p,row in enumerate(m) for z,x in zip(row,terms))-o
           for c,m,o in zip(constant,mmr,offset[1:])]
    assert round(4095*(lut[721]+max(sum(a*v for a,v in zip(matrix[j+1:j+3],delta)) for j in (0,3,6))))==2439
    def vec(xs):return "float2("+",".join(f"{x:.12f}" for x in xs)+")"
    hlsl="static const float2 mmr_constant="+vec(constant)+";\n"
    for label,order in zip(("linear","squared","cubic"),range(3)):
        values=",".join(vec([m[order][i] for m in mmr]) for i in range(7))
        hlsl+=f"static const float2 mmr_{label}[7]={{{values}}};\n"
    for i,label in enumerate(("row_r","row_g","row_b")):
        hlsl+=f"static const float2 {label}="+vec(matrix[3*i+1:3*i+3])+";\n"
    hlsl+="static const float2 chroma_offset="+vec(offset[1:])+";\n"
    bytecode=compile_cs(hlsl+(D/"rt84_hist.hlsl").read_text(encoding="utf8"))
    (D/"rt84_program.inc").write_text("static const float rt84_luma[1024]={\n"+
        ",\n".join(",".join(f"{x:.12f}f" for x in lut[i:i+8]) for i in range(0,1024,8))+
        "\n};\nstatic const unsigned char rt84_bytecode[]={\n"+
        ",\n".join(",".join(f"0x{x:02x}" for x in bytecode[i:i+24]) for i in range(0,len(bytecode),24))+
        "\n};\n",encoding="utf8")


if __name__ == "__main__":
    program()
