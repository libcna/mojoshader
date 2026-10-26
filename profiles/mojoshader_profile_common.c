/**
 * MojoShader; generate shader programs from bytecode of compiled
 *  Direct3D shaders.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#define __MOJOSHADER_INTERNAL__ 1
#include "mojoshader_profile.h"

#pragma GCC visibility push(hidden)

// Common Utilities

void out_of_memory(Context *ctx)
{
    ctx->isfail = ctx->out_of_memory = 1;
} // out_of_memory

void *Malloc(Context *ctx, const size_t len)
{
    void *retval = ctx->malloc((int) len, ctx->malloc_data);
    if (retval == NULL)
        out_of_memory(ctx);
    return retval;
} // Malloc

char *StrDup(Context *ctx, const char *str)
{
    char *retval = (char *) Malloc(ctx, strlen(str) + 1);
    if (retval != NULL)
        strcpy(retval, str);
    return retval;
} // StrDup

void Free(Context *ctx, void *ptr)
{
    ctx->free(ptr, ctx->malloc_data);
} // Free

void * MOJOSHADERCALL MallocBridge(int bytes, void *data)
{
    return Malloc((Context *) data, (size_t) bytes);
} // MallocBridge

void MOJOSHADERCALL FreeBridge(void *ptr, void *data)
{
    Free((Context *) data, ptr);
} // FreeBridge

// Jump between output sections in the context...

int set_output(Context *ctx, Buffer **section)
{
    // only create output sections on first use.
    if (*section == NULL)
    {
        *section = buffer_create(256, MallocBridge, FreeBridge, ctx);
        if (*section == NULL)
            return 0;
    } // if

    ctx->output = *section;
    return 1;
} // set_output

void push_output(Context *ctx, Buffer **section)
{
    // How many times an emitter pushes depends on the shader it is translating, so untrusted
    // bytecode can drive this past the fixed stack. Failing the context here stops emission; the
    // asserts caught nothing in a release build. -CNA
    if (ctx->output_stack_len >= (int) (STATICARRAYLEN(ctx->output_stack)))
    {
        fail(ctx, "Shader output nesting is too deep.");
        return;
    } // if
    ctx->output_stack[ctx->output_stack_len] = ctx->output;
    ctx->indent_stack[ctx->output_stack_len] = ctx->indent;
    ctx->output_stack_len++;
    if (!set_output(ctx, section))
        return;
    ctx->indent = 0;
} // push_output

void pop_output(Context *ctx)
{
    if (ctx->output_stack_len <= 0)
    {
        fail(ctx, "Shader output nesting is unbalanced.");
        return;
    } // if
    ctx->output_stack_len--;
    ctx->output = ctx->output_stack[ctx->output_stack_len];
    ctx->indent = ctx->indent_stack[ctx->output_stack_len];
} // pop_output

// Shader model version magic...

uint32 ver_ui32(const uint8 major, const uint8 minor)
{
    return ( (((uint32) major) << 16) | (((minor) == 0xFF) ? 1 : (minor)) );
} // version_ui32

int shader_version_supported(const uint8 maj, const uint8 min)
{
    return (ver_ui32(maj,min) <= ver_ui32(MAX_SHADER_MAJOR, MAX_SHADER_MINOR));
} // shader_version_supported

int shader_version_atleast(const Context *ctx, const uint8 maj,
                                         const uint8 min)
{
    return (ver_ui32(ctx->major_ver, ctx->minor_ver) >= ver_ui32(maj, min));
} // shader_version_atleast

int shader_version_exactly(const Context *ctx, const uint8 maj,
                           const uint8 min)
{
    return ((ctx->major_ver == maj) && (ctx->minor_ver == min));
} // shader_version_exactly

int shader_is_pixel(const Context *ctx)
{
    return (ctx->shader_type == MOJOSHADER_TYPE_PIXEL);
} // shader_is_pixel

int shader_is_vertex(const Context *ctx)
{
    return (ctx->shader_type == MOJOSHADER_TYPE_VERTEX);
} // shader_is_vertex

// Fail...

int isfail(const Context *ctx)
{
    return ctx->isfail;
} // isfail

void failf(Context *ctx, const char *fmt, ...) ISPRINTF(2,3);
void failf(Context *ctx, const char *fmt, ...)
{
    ctx->isfail = 1;
    if (ctx->out_of_memory)
        return;

    // no filename at this level (we pass a NULL to errorlist_add_va()...)
    va_list ap;
    va_start(ap, fmt);
    errorlist_add_va(ctx->errors, NULL, ctx->current_position, fmt, ap);
    va_end(ap);
} // failf

void fail(Context *ctx, const char *reason)
{
    failf(ctx, "%s", reason);
} // fail

// Output Lines...

void output_line(Context *ctx, const char *fmt, ...) ISPRINTF(2,3);
void output_line(Context *ctx, const char *fmt, ...)
{
    assert(ctx->output != NULL);
    if (isfail(ctx))
        return;  // we failed previously, don't go on...

    const int indent = ctx->indent;
    if (indent > 0)
    {
        char *indentbuf = (char *) alloca(indent);
        memset(indentbuf, '\t', indent);
        buffer_append(ctx->output, indentbuf, indent);
    } // if

    va_list ap;
    va_start(ap, fmt);
    buffer_append_va(ctx->output, fmt, ap);
    va_end(ap);

    buffer_append(ctx->output, ctx->endline, ctx->endline_len);
} // output_line

void output_blank_line(Context *ctx)
{
    assert(ctx->output != NULL);
    if (!isfail(ctx))
        buffer_append(ctx->output, ctx->endline, ctx->endline_len);
} // output_blank_line

/* MOJOSHADER_printFloat already writes the C-locale literal the profiles want, with no
 * redundant zeros ("0.5", "-0.0", "1.0e-10"). The old trimming here could append ".0" after an
 * exponent and refused anything whose untrimmed length was within two of the buffer size. Now
 * a ".0" fraction is dropped only for leavedecimal == 0 (the D3D disassembly's "1", "1e28"), and
 * only a spelling that really does not fit fails. -CNA
 */
void floatstr(Context *ctx, char *buf, size_t bufsize, float f,
              int leavedecimal)
{
    size_t len;
    char *dot;
    char *exponent;
    char *end;
    uint32 bits;

    memcpy(&bits, &f, sizeof (bits));
    if ((bits & 0x7F800000) == 0x7F800000)
    {
        // Infinity or NaN. These used to come out as "inf" (for either sign) or "NaN", which
        // no shader compiler accepts. -CNA
        const int is_nan = ((bits & 0x007FFFFF) != 0);
        const int negative = ((bits & 0x80000000) != 0);
        int written;
        if (bufsize > 0)
            buf[0] = '\0';
        if (ctx->float_from_bits_function != NULL)
        {
            written = snprintf(buf, bufsize, "%s(0x%08xu)",
                               ctx->float_from_bits_function, (uint) bits);
        } // if
        else if ((ctx->float_infinity_literal != NULL) && (!is_nan))
        {
            written = snprintf(buf, bufsize, "%s%s", negative ? "-" : "",
                               ctx->float_infinity_literal);
        } // else if
        else
        {
            failf(ctx, "Float constant %s (0x%08x) cannot be written in this profile: its "
                  "language defines no spelling for it",
                  is_nan ? "NaN" : (negative ? "-inf" : "+inf"), (uint) bits);
            return;
        } // else
        if ((written < 0) || (((size_t) written) >= bufsize))
            fail(ctx, "BUG: internal buffer is too small");
        return;
    } // if

    len = MOJOSHADER_printFloat(buf, bufsize, f);
    if (len >= bufsize)
    {
        fail(ctx, "BUG: internal buffer is too small");
        return;
    } // if

    dot = strchr(buf, '.');
    assert(dot != NULL);  // every finite spelling has one
    exponent = strchr(dot, 'e');
    end = (exponent != NULL) ? exponent : (buf + len);
    if ((!leavedecimal) && (end == dot + 2) && (dot[1] == '0'))
    {
        if (exponent == NULL)
            *dot = '\0';
        else
        {
            size_t i;  // move the exponent over the ".0"
            for (i = 0; exponent[i] != '\0'; i++)
                dot[i] = exponent[i];
            dot[i] = '\0';
        } // else
    } // if
} // floatstr

// Deal with register lists...

static inline uint32 reg_to_ui32(const RegisterType regtype, const int regnum)
{
    return ( ((uint32) regnum) | (((uint32) regtype) << 16) );
} // reg_to_uint32

// !!! FIXME: ditch this for a hash table.
RegisterList *reglist_insert(Context *ctx, RegisterList *prev,
                             const RegisterType regtype,
                             const int regnum)
{
    const uint32 newval = reg_to_ui32(regtype, regnum);
    RegisterList *item = prev->next;
    while (item != NULL)
    {
        const uint32 val = reg_to_ui32(item->regtype, item->regnum);
        if (newval == val)
            return item;  // already set, so we're done.
        else if (newval < val)  // insert it here.
            break;
        else // if (newval > val)
        {
            // keep going, we're not to the insertion point yet.
            prev = item;
            item = item->next;
        } // else
    } // while

    // we need to insert an entry after (prev).
    item = (RegisterList *) Malloc(ctx, sizeof (RegisterList));
    if (item != NULL)
    {
        item->regtype = regtype;
        item->regnum = regnum;
        item->usage = MOJOSHADER_USAGE_UNKNOWN;
        item->index = 0;
        item->writemask = 0;
        item->misc = 0;
        item->written = 0;
#if SUPPORT_PROFILE_SPIRV
        item->spirv.iddecl = 0;
        item->spirv.is_ssa = 0;
#endif
        item->array = NULL;
        item->next = prev->next;
        prev->next = item;
    } // if

    return item;
} // reglist_insert

RegisterList *reglist_find(const RegisterList *prev,
                           const RegisterType rtype,
                           const int regnum)
{
    const uint32 newval = reg_to_ui32(rtype, regnum);
    RegisterList *item = prev->next;
    while (item != NULL)
    {
        const uint32 val = reg_to_ui32(item->regtype, item->regnum);
        if (newval == val)
            return item;  // here it is.
        else if (newval < val)  // should have been here if it existed.
            return NULL;
        else // if (newval > val)
            item = item->next;
    } // while

    return NULL;  // wasn't in the list.
} // reglist_find

RegisterList *set_used_register(Context *ctx,
                                const RegisterType regtype,
                                const int regnum,
                                const int written)
{
    RegisterList *reg = NULL;
    if ((regtype == REG_TYPE_COLOROUT) && (regnum > 0))
        ctx->have_multi_color_outputs = 1;

    reg = reglist_insert(ctx, &ctx->used_registers, regtype, regnum);
    if (reg && written)
        reg->written = 0xF;
    return reg;
} // set_used_register

void set_defined_register(Context *ctx, const RegisterType rtype,
                          const int regnum)
{
    reglist_insert(ctx, &ctx->defined_registers, rtype, regnum);
} // set_defined_register

// Writemasks

int writemask_xyzw(const int writemask)
{
    return (writemask == 0xF);  // 0xF == 1111. No explicit mask (full!).
} // writemask_xyzw

int writemask_xyz(const int writemask)
{
    return (writemask == 0x7);  // 0x7 == 0111. (that is: xyz)
} // writemask_xyz

int writemask_xy(const int writemask)
{
    return (writemask == 0x3);  // 0x3 == 0011. (that is: xy)
} // writemask_xy

int writemask_x(const int writemask)
{
    return (writemask == 0x1);  // 0x1 == 0001. (that is: x)
} // writemask_x

int writemask_y(const int writemask)
{
    return (writemask == 0x2);  // 0x2 == 0010. (that is: y)
} // writemask_y

int replicate_swizzle(const int swizzle)
{
    // elements 1|2 match 3|4 and element 1 matches element 2.
    return ( (((swizzle >> 4) & 0xF) == ((swizzle >> 0) & 0xF)) &&
             (((swizzle >> 0) & 0x3) == ((swizzle >> 2) & 0x3)) );
} // replicate_swizzle

int no_swizzle(const int swizzle)
{
    return (swizzle == 0xE4);  // 0xE4 == 11100100 ... 0 1 2 3. No swizzle.
} // no_swizzle

int vecsize_from_writemask(const int m)
{
    return (m & 1) + ((m >> 1) & 1) + ((m >> 2) & 1) + ((m >> 3) & 1);
} // vecsize_from_writemask

void set_dstarg_writemask(DestArgInfo *dst, const int mask)
{
    dst->writemask = mask;
    dst->writemask0 = ((mask >> 0) & 1);
    dst->writemask1 = ((mask >> 1) & 1);
    dst->writemask2 = ((mask >> 2) & 1);
    dst->writemask3 = ((mask >> 3) & 1);
} // set_dstarg_writemask

// D3D stuff that's used in more than just the d3d profile...

int isscalar(Context *ctx, const MOJOSHADER_shaderType shader_type,
             const RegisterType rtype, const int rnum)
{
    const int uses_psize = ctx->uses_pointsize;
    const int uses_fog = ctx->uses_fog;
    if ( (rtype == REG_TYPE_OUTPUT) && ((uses_psize) || (uses_fog)) )
    {
        const RegisterList *reg = reglist_find(&ctx->attributes, rtype, rnum);
        if (reg != NULL)
        {
            const MOJOSHADER_usage usage = reg->usage;
            return ( (uses_psize && (usage == MOJOSHADER_USAGE_POINTSIZE)) ||
                     (uses_fog && (usage == MOJOSHADER_USAGE_FOG)) );
        } // if
    } // if

    return scalar_register(shader_type, rtype, rnum);
} // isscalar

const char *get_D3D_register_string(Context *ctx,
                                    RegisterType regtype,
                                    int regnum, char *regnum_str,
                                    size_t regnum_size)
{
    const char *retval = NULL;
    int has_number = 1;

    switch (regtype)
    {
        case REG_TYPE_TEMP:
            retval = "r";
            break;

        case REG_TYPE_INPUT:
            retval = "v";
            break;

        case REG_TYPE_CONST:
            retval = "c";
            break;

        case REG_TYPE_ADDRESS:  // (or REG_TYPE_TEXTURE, same value.)
            retval = shader_is_vertex(ctx) ? "a" : "t";
            break;

        case REG_TYPE_RASTOUT:
            switch ((RastOutType) regnum)
            {
                case RASTOUT_TYPE_POSITION: retval = "oPos"; break;
                case RASTOUT_TYPE_FOG: retval = "oFog"; break;
                case RASTOUT_TYPE_POINT_SIZE: retval = "oPts"; break;
            } // switch
            has_number = 0;
            break;

        case REG_TYPE_ATTROUT:
            retval = "oD";
            break;

        case REG_TYPE_OUTPUT: // (or REG_TYPE_TEXCRDOUT, same value.)
            if (shader_is_vertex(ctx) && shader_version_atleast(ctx, 3, 0))
                retval = "o";
            else
                retval = "oT";
            break;

        case REG_TYPE_CONSTINT:
            retval = "i";
            break;

        case REG_TYPE_COLOROUT:
            retval = "oC";
            break;

        case REG_TYPE_DEPTHOUT:
            retval = "oDepth";
            has_number = 0;
            break;

        case REG_TYPE_SAMPLER:
            retval = "s";
            break;

        case REG_TYPE_CONSTBOOL:
            retval = "b";
            break;

        case REG_TYPE_LOOP:
            retval = "aL";
            has_number = 0;
            break;

        case REG_TYPE_MISCTYPE:
            switch ((const MiscTypeType) regnum)
            {
                case MISCTYPE_TYPE_POSITION: retval = "vPos"; break;
                case MISCTYPE_TYPE_FACE: retval = "vFace"; break;
            } // switch
            has_number = 0;
            break;

        case REG_TYPE_LABEL:
            retval = "l";
            break;

        case REG_TYPE_PREDICATE:
            retval = "p";
            break;

        //case REG_TYPE_TEMPFLOAT16:  // !!! FIXME: don't know this asm string
        default:
            fail(ctx, "unknown register type");
            retval = "???";
            has_number = 0;
            break;
    } // switch

    if (has_number)
        snprintf(regnum_str, regnum_size, "%u", (uint) regnum);
    else
        regnum_str[0] = '\0';

    return retval;
} // get_D3D_register_string

// !!! FIXME: These should stay in the mojoshader_profile_d3d file
// !!! FIXME: but ARB1 relies on them, so we have to move them here.
// !!! FIXME: If/when we kill off ARB1, we can move these back.

const char *get_D3D_varname_in_buf(Context *ctx, RegisterType rt,
                                   int regnum, char *buf,
                                   const size_t len)
{
    char regnum_str[16];
    const char *regtype_str = get_D3D_register_string(ctx, rt, regnum,
                                              regnum_str, sizeof (regnum_str));
    snprintf(buf,len,"%s%s", regtype_str, regnum_str);
    return buf;
} // get_D3D_varname_in_buf


const char *get_D3D_varname(Context *ctx, RegisterType rt, int regnum)
{
    char buf[64];
    get_D3D_varname_in_buf(ctx, rt, regnum, buf, sizeof (buf));
    return StrDup(ctx, buf);
} // get_D3D_varname

#pragma GCC visibility pop
