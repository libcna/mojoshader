/**
 * A stand-in for <d3d11.h>, for cna_d3d11_tests only: just the types, constants and COM calls
 * mojoshader_d3d11.c uses, so the glue compiles on a host without Direct3D and the test can hand
 * it fake device and context objects. It is not the real API -- every interface has only the
 * methods the glue calls, in an order of this file's choosing -- and nothing else may include it.
 *
 * This file is part of the libcna fork of MojoShader; see tests/cna/README.md.
 */

#ifndef MOJOSHADER_CNA_D3D11_STUB_H
#define MOJOSHADER_CNA_D3D11_STUB_H

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <dlfcn.h>

typedef int32_t HRESULT;
typedef uint32_t UINT;
typedef uint32_t ULONG;
typedef size_t SIZE_T;
typedef const char *LPCSTR;
typedef const void *LPCVOID;
typedef void *HMODULE;
typedef const struct CNA_GUID { uint32_t data[4]; } *REFIID;
typedef struct ID3DInclude ID3DInclude;
typedef struct D3D_SHADER_MACRO { LPCSTR Name; LPCSTR Definition; } D3D_SHADER_MACRO;

typedef enum DXGI_FORMAT
{
    DXGI_FORMAT_UNKNOWN = 0,
    DXGI_FORMAT_R32G32B32A32_UINT = 3, DXGI_FORMAT_R32G32B32A32_SINT = 4,
    DXGI_FORMAT_R32G32B32_UINT = 7, DXGI_FORMAT_R32G32B32_SINT = 8,
    DXGI_FORMAT_R16G16B16A16_UINT = 12, DXGI_FORMAT_R16G16B16A16_SINT = 14,
    DXGI_FORMAT_R32G32_UINT = 17, DXGI_FORMAT_R32G32_SINT = 18,
    DXGI_FORMAT_R10G10B10A2_UINT = 25,
    DXGI_FORMAT_R8G8B8A8_UINT = 30, DXGI_FORMAT_R8G8B8A8_SINT = 32,
    DXGI_FORMAT_R16G16_UINT = 36, DXGI_FORMAT_R16G16_SINT = 38,
    DXGI_FORMAT_R32_UINT = 42, DXGI_FORMAT_R32_SINT = 43,
    DXGI_FORMAT_R8G8_UINT = 50, DXGI_FORMAT_R8G8_SINT = 52,
    DXGI_FORMAT_R16_UINT = 57, DXGI_FORMAT_R16_SINT = 59,
    DXGI_FORMAT_R8_UINT = 62, DXGI_FORMAT_R8_SINT = 64
} DXGI_FORMAT;

typedef struct D3D11_INPUT_ELEMENT_DESC
{
    LPCSTR SemanticName;
    UINT SemanticIndex;
    DXGI_FORMAT Format;
    UINT InputSlot;
    UINT AlignedByteOffset;
    UINT InputSlotClass;
    UINT InstanceDataStepRate;
} D3D11_INPUT_ELEMENT_DESC;

#define D3D11_USAGE_DYNAMIC 2
#define D3D11_BIND_CONSTANT_BUFFER 0x4
#define D3D11_CPU_ACCESS_WRITE 0x10000
#define D3D11_MAP_WRITE_DISCARD 4

typedef struct D3D11_BUFFER_DESC
{
    UINT ByteWidth;
    UINT Usage;
    UINT BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
    UINT StructureByteStride;
} D3D11_BUFFER_DESC;

typedef struct D3D11_SUBRESOURCE_DATA
{
    const void *pSysMem;
    UINT SysMemPitch;
    UINT SysMemSlicePitch;
} D3D11_SUBRESOURCE_DATA;

typedef struct D3D11_MAPPED_SUBRESOURCE
{
    void *pData;
    UINT RowPitch;
    UINT DepthPitch;
} D3D11_MAPPED_SUBRESOURCE;

typedef struct ID3D11Buffer ID3D11Buffer;
typedef struct ID3D11VertexShader ID3D11VertexShader;
typedef struct ID3D11PixelShader ID3D11PixelShader;
typedef struct ID3D11Device ID3D11Device;
typedef struct ID3D11DeviceContext ID3D11DeviceContext;
typedef struct ID3D11Resource ID3D11Resource;  /* a buffer, as the glue casts it */

typedef struct ID3D11ReleaseVtbl { ULONG (*Release)(void *This); } ID3D11ReleaseVtbl;
struct ID3D11Buffer { const ID3D11ReleaseVtbl *lpVtbl; };
struct ID3D11VertexShader { const ID3D11ReleaseVtbl *lpVtbl; };
struct ID3D11PixelShader { const ID3D11ReleaseVtbl *lpVtbl; };

typedef struct ID3D11DeviceVtbl
{
    HRESULT (*CreateBuffer)(ID3D11Device *This, const D3D11_BUFFER_DESC *desc,
                            const D3D11_SUBRESOURCE_DATA *initial, ID3D11Buffer **buffer);
    HRESULT (*CreateVertexShader)(ID3D11Device *This, const void *bytecode, SIZE_T length,
                                  void *linkage, ID3D11VertexShader **shader);
    HRESULT (*CreatePixelShader)(ID3D11Device *This, const void *bytecode, SIZE_T length,
                                 void *linkage, ID3D11PixelShader **shader);
} ID3D11DeviceVtbl;
struct ID3D11Device { const ID3D11DeviceVtbl *lpVtbl; };

typedef struct ID3D11DeviceContextVtbl
{
    HRESULT (*Map)(ID3D11DeviceContext *This, ID3D11Resource *resource, UINT subresource,
                   UINT type, UINT flags, D3D11_MAPPED_SUBRESOURCE *mapped);
    void (*Unmap)(ID3D11DeviceContext *This, ID3D11Resource *resource, UINT subresource);
    void (*VSSetShader)(ID3D11DeviceContext *This, ID3D11VertexShader *shader, void *classes,
                        UINT count);
    void (*VSSetConstantBuffers)(ID3D11DeviceContext *This, UINT slot, UINT count,
                                 ID3D11Buffer *const *buffers);
    void (*PSSetShader)(ID3D11DeviceContext *This, ID3D11PixelShader *shader, void *classes,
                        UINT count);
    void (*PSSetConstantBuffers)(ID3D11DeviceContext *This, UINT slot, UINT count,
                                 ID3D11Buffer *const *buffers);
} ID3D11DeviceContextVtbl;
struct ID3D11DeviceContext { const ID3D11DeviceContextVtbl *lpVtbl; };

#define ID3D11Buffer_Release(This) ((This)->lpVtbl->Release(This))
#define ID3D11VertexShader_Release(This) ((This)->lpVtbl->Release(This))
#define ID3D11PixelShader_Release(This) ((This)->lpVtbl->Release(This))
#define ID3D11Device_CreateBuffer(This, desc, initial, buffer) \
    ((This)->lpVtbl->CreateBuffer(This, desc, initial, buffer))
#define ID3D11Device_CreateVertexShader(This, code, length, linkage, shader) \
    ((This)->lpVtbl->CreateVertexShader(This, code, length, linkage, shader))
#define ID3D11Device_CreatePixelShader(This, code, length, linkage, shader) \
    ((This)->lpVtbl->CreatePixelShader(This, code, length, linkage, shader))
#define ID3D11DeviceContext_Map(This, resource, sub, type, flags, mapped) \
    ((This)->lpVtbl->Map(This, resource, sub, type, flags, mapped))
#define ID3D11DeviceContext_Unmap(This, resource, sub) \
    ((This)->lpVtbl->Unmap(This, resource, sub))
#define ID3D11DeviceContext_VSSetShader(This, shader, classes, count) \
    ((This)->lpVtbl->VSSetShader(This, shader, classes, count))
#define ID3D11DeviceContext_VSSetConstantBuffers(This, slot, count, buffers) \
    ((This)->lpVtbl->VSSetConstantBuffers(This, slot, count, buffers))
#define ID3D11DeviceContext_PSSetShader(This, shader, classes, count) \
    ((This)->lpVtbl->PSSetShader(This, shader, classes, count))
#define ID3D11DeviceContext_PSSetConstantBuffers(This, slot, count, buffers) \
    ((This)->lpVtbl->PSSetConstantBuffers(This, slot, count, buffers))

/* ID3D10Blob is the glue's own VKD3DBlob off Windows; these reach it through its vtable. */
#define ID3D10Blob_GetBufferPointer(This) ((This)->lpVtbl->GetBufferPointer(This))
#define ID3D10Blob_GetBufferSize(This) ((This)->lpVtbl->GetBufferSize(This))
#define ID3D10Blob_Release(This) ((This)->lpVtbl->Release(This))

#endif /* MOJOSHADER_CNA_D3D11_STUB_H */
