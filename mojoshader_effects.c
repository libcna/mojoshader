/**
 * MojoShader; generate shader programs from bytecode of compiled
 *  Direct3D shaders.
 *
 * Please see the file LICENSE.txt in the source's root directory.
 *
 *  This file written by Ryan C. Gordon.
 */

#define __MOJOSHADER_INTERNAL__ 1
#include "mojoshader_internal.h"

#ifdef MOJOSHADER_EFFECT_SUPPORT

#ifndef MOJOSHADER_USE_SDL_STDLIB
#include <math.h>
#endif /* MOJOSHADER_USE_SDL_STDLIB */

/* Every MojoShader backend sizes its register files with these same limits (MAX_REG_FILE_* in
 * mojoshader_opengl.c, mojoshader_d3d11.c and mojoshader_sdlgpu.c). The effect layer writes into
 * those files through register indices taken from parsed content, so it needs the same ceiling
 * to bound them against. -CNA
 */
#define MOJOSHADER_EFFECT_MAX_REG_FILE_F 8192
#define MOJOSHADER_EFFECT_MAX_REG_FILE_I 2047
#define MOJOSHADER_EFFECT_MAX_REG_FILE_B 2047

static void run_preshader(const MOJOSHADER_preshader *preshader,
                          float *outregs,
                          unsigned int outregs_count)
{
    const float *inregs = preshader->registers;
    // preshader->register_count is in vec4-register units (matches copy_parameter_data's "wanted"
    // computation below); inregs is indexed in floats, so the bound needs the same <<2 conversion
    // outregs_count already gets from its caller, or any struct/array preshader input past the
    // first register is rejected as out-of-bounds and the whole preshader silently aborts. -CNA
    const unsigned int inregs_count = preshader->register_count << 2;

    // this is fairly straightforward, as there aren't any branching
    //  opcodes in the preshader instruction set (at the moment, at least).
    const int scalarstart = (int) MOJOSHADER_PRESHADEROP_SCALAR_OPS;

    double *temps = NULL;
    // The parser bounds this too; this is the second line of defence, because the cost of being
    // wrong here is the process stack rather than a bad pixel. -CNA
    if (preshader->temp_count > 8192)
        return;
    if (preshader->temp_count > 0)
    {
        temps = (double *) alloca(sizeof (double) * preshader->temp_count);
        memset(temps, '\0', sizeof (double) * preshader->temp_count);
    } // if

    double dst[4] = { 0, 0, 0, 0 };
    double src[3][4] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { 0, 0, 0, 0 } };
    const double *src0 = &src[0][0];
    const double *src1 = &src[1][0];
    const double *src2 = &src[2][0];

    MOJOSHADER_preshaderInstruction *inst = preshader->instructions;
    int instit;

#if 0 // FIXME: Do we need to do this or is the compiler smart enough?
    // Clear preshader output registers first!
    for (instit = 0; instit < preshader->instruction_count; instit++, inst++)
    {
        const MOJOSHADER_preshaderOperand *operand = &inst->operands[inst->operand_count - 1];
        if (operand->type == MOJOSHADER_PRESHADEROPERAND_OUTPUT)
            memset(&outregs[operand->index], '\0', sizeof(float) * 4);
    } // for
    inst = preshader->instructions;
#endif

    for (instit = 0; instit < preshader->instruction_count; instit++, inst++)
    {
        const MOJOSHADER_preshaderOperand *operand = inst->operands;
        const int elems = inst->element_count;
        const int elemsbytes = sizeof (double) * elems;
        const int isscalarop = (inst->opcode >= scalarstart);

        // Instruction fields come from parsed content, so an out-of-range element count skips
        // the instruction instead of tripping an assert or indexing a four-wide operand. -CNA
        if ((elems < 0) || (elems > 4)) continue;
        if ((inst->operand_count < 1) ||
            (inst->operand_count > STATICARRAYLEN(inst->operands))) continue;

        // load up our operands...
        int opiter, elemiter;
        for (opiter = 0; opiter < inst->operand_count-1; opiter++, operand++)
        {
            const int isscalar = ((isscalarop) && (opiter == 0));
            const unsigned int index = operand->index;
            switch (operand->type)
            {
                case MOJOSHADER_PRESHADEROPERAND_LITERAL:
                {
                    // Every operand index below is parsed content, so it is bounded against the
                    // array it addresses rather than asserted. -CNA
                    const unsigned int want = isscalar ? 1u : (unsigned int) elems;
                    // Subtraction rather than addition: an index near UINT_MAX would make
                    // (index + want) wrap and pass an addition-based check. -CNA
                    if ((index >= preshader->literal_count) ||
                        ((preshader->literal_count - index) < want)) return;
                    if (!isscalar)
                        memcpy(&src[opiter][0], &preshader->literals[index], elemsbytes);
                    else
                    {
                        for (elemiter = 0; elemiter < elems; elemiter++)
                            src[opiter][elemiter] = preshader->literals[index];
                    } // else
                    break;
                } // case

                case MOJOSHADER_PRESHADEROPERAND_INPUT:
                    if (operand->array_register_count > 0)
                    {
                        int i;
                        const int *regsi = (const int *) inregs;
                        const unsigned int base = ((index >> 4) * 4) + ((index >> 2) & 3);
                        int arrIndex;
                        if (base >= inregs_count) return;
                        arrIndex = regsi[base];
                        for (i = 0; i < operand->array_register_count; i++)
                        {
                            // Both terms come from content (the register table and a runtime
                            // parameter value): add them where the sum cannot overflow, then
                            // bound it. -CNA
                            const long long next = (long long) operand->array_registers[i] +
                                                   (long long) arrIndex;
                            if ((next < 0) || (next >= (long long) inregs_count)) return;
                            arrIndex = regsi[next];
                        }
                        src[opiter][0] = arrIndex;
                    } // if
                    else if (isscalar)
                    {
                        if (index >= inregs_count) return;
                        src[opiter][0] = inregs[index];
                    } // else if
                    else
                    {
                        int cpy;
                        if ((index >= inregs_count) ||
                            ((inregs_count - index) < (unsigned int) elems)) return;
                        for (cpy = 0; cpy < elems; cpy++)
                            src[opiter][cpy] = inregs[index+cpy];
                    } // else
                    break;

                case MOJOSHADER_PRESHADEROPERAND_OUTPUT:
                {
                    const unsigned int want = isscalar ? 1u : (unsigned int) elems;
                    if ((outregs_count != 0) &&
                        ((index >= outregs_count) || ((outregs_count - index) < want))) return;
                    if (isscalar)
                        src[opiter][0] = outregs[index];
                    else
                    {
                        int cpy;
                        for (cpy = 0; cpy < elems; cpy++)
                            src[opiter][cpy] = outregs[index+cpy];
                    } // else
                    break;
                } // case

                case MOJOSHADER_PRESHADEROPERAND_TEMP:
                    if (temps != NULL)
                    {
                        const unsigned int want = isscalar ? 1u : (unsigned int) elems;
                        if ((index >= preshader->temp_count) ||
                            ((preshader->temp_count - index) < want)) return;
                        if (isscalar)
                            src[opiter][0] = temps[index];
                        else
                            memcpy(src[opiter], temps + index, elemsbytes);
                    } // if
                    break;

                default:
                    // Untrusted content can name an operand type the parser never assigned.
                    return;
            } // switch
        } // for

        // run the actual instruction, store result to dst.
        int i;
        switch (inst->opcode)
        {
            #define OPCODE_CASE(op, val) \
                case MOJOSHADER_PRESHADEROP_##op: \
                    for (i = 0; i < elems; i++) { dst[i] = val; } \
                    break;

            //OPCODE_CASE(NOP, 0.0)  // not a real instruction.
            OPCODE_CASE(MOV, src0[i])
            OPCODE_CASE(NEG, -src0[i])
            OPCODE_CASE(RCP, 1.0 / src0[i])
            OPCODE_CASE(FRC, src0[i] - floor(src0[i]))
            OPCODE_CASE(EXP, exp(src0[i]))
            OPCODE_CASE(LOG, log(src0[i]))
            OPCODE_CASE(RSQ, 1.0 / sqrt(src0[i]))
            OPCODE_CASE(SIN, sin(src0[i]))
            OPCODE_CASE(COS, cos(src0[i]))
            OPCODE_CASE(ASIN, asin(src0[i]))
            OPCODE_CASE(ACOS, acos(src0[i]))
            OPCODE_CASE(ATAN, atan(src0[i]))
            OPCODE_CASE(MIN, (src0[i] < src1[i]) ? src0[i] : src1[i])
            OPCODE_CASE(MAX, (src0[i] > src1[i]) ? src0[i] : src1[i])
            OPCODE_CASE(LT, (src0[i] < src1[i]) ? 1.0 : 0.0)
            OPCODE_CASE(GE, (src0[i] >= src1[i]) ? 1.0 : 0.0)
            OPCODE_CASE(ADD, src0[i] + src1[i])
            OPCODE_CASE(MUL,  src0[i] * src1[i])
            OPCODE_CASE(ATAN2, atan2(src0[i], src1[i]))
            OPCODE_CASE(DIV, src0[i] / src1[i])
            OPCODE_CASE(CMP, (src0[i] >= 0.0) ? src1[i] : src2[i])
            //OPCODE_CASE(NOISE, ???)  // !!! FIXME: don't know what this does
            //OPCODE_CASE(MOVC, ???)  // !!! FIXME: don't know what this does
            OPCODE_CASE(MIN_SCALAR, (src0[0] < src1[i]) ? src0[0] : src1[i])
            OPCODE_CASE(MAX_SCALAR, (src0[0] > src1[i]) ? src0[0] : src1[i])
            OPCODE_CASE(LT_SCALAR, (src0[0] < src1[i]) ? 1.0 : 0.0)
            OPCODE_CASE(GE_SCALAR, (src0[0] >= src1[i]) ? 1.0 : 0.0)
            OPCODE_CASE(ADD_SCALAR, src0[0] + src1[i])
            OPCODE_CASE(MUL_SCALAR, src0[0] * src1[i])
            OPCODE_CASE(ATAN2_SCALAR, atan2(src0[0], src1[i]))
            OPCODE_CASE(DIV_SCALAR, src0[0] / src1[i])
            //OPCODE_CASE(DOT_SCALAR)  // !!! FIXME: isn't this just a MUL?
            //OPCODE_CASE(NOISE_SCALAR, ???)  // !!! FIXME: ?
            #undef OPCODE_CASE

            case MOJOSHADER_PRESHADEROP_DOT:
            {
                double final = 0.0;
                for (i = 0; i < elems; i++)
                    final += src0[i] * src1[i];
                for (i = 0; i < elems; i++)
                    dst[i] = final;  // !!! FIXME: is this right?
                break;
            } // case

            default:
                // The opcode came from parsed content; stop rather than assert. -CNA
                return;
        } // switch

        // Figure out where dst wants to be stored. The destination index is parsed content too,
        // so it is bounded rather than asserted. -CNA
        if (operand->type == MOJOSHADER_PRESHADEROPERAND_TEMP)
        {
            if ((temps == NULL) || (operand->index >= preshader->temp_count) ||
                ((preshader->temp_count - operand->index) < (unsigned int) elems))
            {
                return;
            } // if
            memcpy(temps + operand->index, dst, elemsbytes);
        } // if
        else if (operand->type == MOJOSHADER_PRESHADEROPERAND_OUTPUT)
        {
            if ((outregs_count != 0) &&
                ((operand->index >= outregs_count) ||
                 ((outregs_count - operand->index) < (unsigned int) elems)))
            {
                return;
            } // if
            for (i = 0; i < elems; i++)
                outregs[operand->index + i] = (float) dst[i];
        } // else if
        else
        {
            return;
        } // else
    } // for
} // run_preshader

static MOJOSHADER_effect MOJOSHADER_out_of_mem_effect = {
    1, &MOJOSHADER_out_of_mem_error, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static MOJOSHADER_error MOJOSHADER_need_a_backend_error = {
    "Need a MOJOSHADER_effectShaderContext", NULL, MOJOSHADER_POSITION_NONE
};
static MOJOSHADER_effect MOJOSHADER_need_a_backend_effect = {
    1, &MOJOSHADER_need_a_backend_error, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static MOJOSHADER_error MOJOSHADER_unexpected_eof_error = {
    "Unexpected EOF", NULL, MOJOSHADER_POSITION_NONE
};
static MOJOSHADER_effect MOJOSHADER_unexpected_eof_effect = {
    1, &MOJOSHADER_unexpected_eof_error, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static MOJOSHADER_error MOJOSHADER_not_an_effect_error = {
    "Not an Effects Framework binary", NULL, MOJOSHADER_POSITION_NONE
};
static MOJOSHADER_effect MOJOSHADER_not_an_effect_effect = {
    1, &MOJOSHADER_not_an_effect_error, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// MOJOSHADER_compileEffect() returns these four statically allocated effects for its errors. They
// own nothing and have no allocator, so freeing or cloning one must be a no-op. -CNA
static int effect_is_static_error(const MOJOSHADER_effect *effect)
{
    return ((effect == &MOJOSHADER_out_of_mem_effect) ||
            (effect == &MOJOSHADER_need_a_backend_effect) ||
            (effect == &MOJOSHADER_unexpected_eof_effect) ||
            (effect == &MOJOSHADER_not_an_effect_effect));
} // effect_is_static_error

static void push_errors(ErrorList *list, MOJOSHADER_error *errors, int len)
{
    int i;
    for (i = 0; i < len; i += 1)
        errorlist_add(list, errors[i].filename, errors[i].error_position, errors[i].error);
} // push_errors

static void read_version_token(const uint8 **_ptr, uint32 *_len, uint16 *magic, uint8 *version_major, uint8 *version_minor)
{
    const uint32 *ptr = (const uint32 *) *_ptr;
    const uint32 token = SWAP32(*ptr);
    *_ptr += sizeof (token);
    *_len -= sizeof (token);

    *magic = ((token >> 16) & 0xFFFF);
    *version_major = (uint8) ((token >> 8) & 0xFF);
    *version_minor = (uint8) (token & 0xFF);
} // read_version_token

static uint32 readui32(const uint8 **_ptr, uint32 *_len)
{
    uint32 retval = 0;
    if (*_len < sizeof (retval))
        *_len = 0;
    else
    {
        const uint32 *ptr = (const uint32 *) *_ptr;
        retval = SWAP32(*ptr);
        *_ptr += sizeof (retval);
        *_len -= sizeof (retval);
    } // else
    return retval;
} // readui32

/* Upstream took a base pointer and an offset but no length, and left two FIXMEs about it:
 * "sanity checks!" and "verify this doesn't go past EOF looking for a null." Both matter for
 * untrusted content -- the length prefix chooses how much is copied, and the copy was not
 * terminated, so a later strlen() ran off the end of the allocation. base_len is the number of
 * bytes available from base; zero means the caller could not report one. -CNA
 */
static char *readstring(const uint8 *base,
                        const uint32 base_len,
                        const uint32 offset,
                        MOJOSHADER_malloc m,
                        void *d)
{
    uint8 *pair;
    const uint32 *lenptr;
    char *strptr;
    uint32 len;
    char *result = NULL;

    if (base_len != 0)
    {
        if (offset > base_len) return NULL;
        if ((base_len - offset) < sizeof (uint32)) return NULL;
    } // if

    pair = (uint8 *) (base + offset);
    lenptr = (const uint32 *) pair;
    strptr = (char *) (pair + sizeof(uint32));
    len = SWAP32(*lenptr);

    if (len == 0) return NULL; /* No length? No string. */
    if (base_len != 0)
    {
        const uint32 available = base_len - offset - (uint32) sizeof (uint32);
        if (len > available) len = available;
        if (len == 0) return NULL;
    } // if

    result = (char *) m(len + 1, d);
    if (result == NULL) return NULL;
    memcpy(result, strptr, len);
    result[len] = '\0';  /* the source is not guaranteed to carry its own terminator */
    return result;
} // readstring

static int findparameter(const MOJOSHADER_effectParam *params,
                         const uint32 param_count,
                         const char *name,
                         ErrorList *errors)
{
    int i;
    for (i = 0; i < param_count; i++)
        if ((name != NULL) && (params[i].value.name != NULL) &&
            (strcmp(name, params[i].value.name) == 0))
            return i;
    errorlist_add(errors, NULL, 0, "Shader parameter not found in effect.");
    return -1;
}

// readvalue and copyvalue decide a value's storage layout from its class first and its type
// second: only an OBJECT of a sampler type is allocated as an array of MOJOSHADER_effectSamplerState.
// Class and type are independent fields of the content, so anything that later reads valuesSS has
// to ask that same two-part question. Asking about the type alone reinterprets, say, a small array
// of floats as an array of much larger sampler-state structures. -CNA
static int value_is_sampler(const MOJOSHADER_effectValue *value)
{
    return (value->type.parameter_class == MOJOSHADER_SYMCLASS_OBJECT)
        && (value->type.parameter_type >= MOJOSHADER_SYMTYPE_SAMPLER)
        && (value->type.parameter_type <= MOJOSHADER_SYMTYPE_SAMPLERCUBE);
} // value_is_sampler

// How deeply readvalue may recurse through nested sampler-state values before it refuses the
// content as runaway. XNA nests a sampler state's value at most one level; a handful of levels is
// already far past anything legitimate, and the ceiling stops a cycle from exhausting the stack.
// -CNA
#define MOJOSHADER_EFFECT_MAX_VALUE_DEPTH 32

// readvalue decodes a value's type metadata (from base + typeoffset) and its default data (from
// base + valoffset), both offsets into the base region that compileEffect bounds with base_len.
// Upstream read both through fabricated 9,999,999-byte windows and indexed objects[] with no
// count, so a damaged offset, dimension or object reference read or wrote past the buffer. Every
// span is now measured against base_len, every object index against object_count, and the nesting
// against a depth ceiling; it returns 0 when the content cannot be decoded safely so the caller
// can reject the effect. A base_len of 0 means the caller could not supply one and keeps the old
// unbounded behaviour (compileEffect always supplies it). -CNA
static int readvalue(const uint8 *base,
                     const uint32 base_len,
                     const uint32 typeoffset,
                     const uint32 valoffset,
                     MOJOSHADER_effectValue *value,
                     MOJOSHADER_effectObject *objects,
                     const uint32 object_count,
                     const int depth,
                     MOJOSHADER_malloc m,
                     void *d)
{
    int i, j, k;

    if (depth > MOJOSHADER_EFFECT_MAX_VALUE_DEPTH)
        return 0;
    if ((base_len != 0) && ((typeoffset > base_len) || (valoffset > base_len)))
        return 0;

    const uint8 *typeptr = base + typeoffset;
    const uint8 *valptr = base + valoffset;
    unsigned int typelen = (base_len != 0) ? (base_len - typeoffset) : 9999999;
    unsigned int vallen = (base_len != 0) ? (base_len - valoffset) : 9999999;
    const uint32 type = readui32(&typeptr, &typelen);
    const uint32 valclass = readui32(&typeptr, &typelen);
    const uint32 name = readui32(&typeptr, &typelen);
    const uint32 semantic = readui32(&typeptr, &typelen);
    const uint32 numelements = readui32(&typeptr, &typelen);

    value->type.parameter_type = (MOJOSHADER_symbolType) type;
    value->type.parameter_class = (MOJOSHADER_symbolClass) valclass;
    value->name = readstring(base, base_len, name, m, d);
    value->semantic = readstring(base, base_len, semantic, m, d);
    value->type.elements = numelements;

    if (valclass == MOJOSHADER_SYMCLASS_SCALAR
     || valclass == MOJOSHADER_SYMCLASS_VECTOR
     || valclass == MOJOSHADER_SYMCLASS_MATRIX_ROWS
     || valclass == MOJOSHADER_SYMCLASS_MATRIX_COLUMNS)
    {
        const uint32 columncount = readui32(&typeptr, &typelen);
        const uint32 rowcount = readui32(&typeptr, &typelen);

        value->type.columns = columncount;
        value->type.rows = rowcount;

        // Each row is stored padded to one four-float register, and columncount words of it are
        // copied from the content: a row wider than four would copy past its register, the last
        // one past the allocation, and a columncount of 2^30 or more wrapped the byte counts
        // below. No Direct3D 9 type has more than four rows or columns, so both are capped
        // before anything is computed from them. -CNA
        if ((columncount > 4) || (rowcount > 4))
            return 0;

        // value_count is 4 * rowcount padded registers, times the array length; the source read
        // is columncount words per register row. Both come from content: compute them wide,
        // refuse anything that would overflow or outrun the base region, then copy. -CNA
        uint64 rows = (uint64) rowcount;
        if (numelements > 0)
            rows *= numelements;
        const uint64 vcount = rows * 4u;
        const uint64 srcspan = ((uint64) columncount << 2) * rows;
        if ((base_len != 0) &&
            ((vcount > base_len) || ((uint64) valoffset + srcspan > base_len)))
            return 0;
        // The allocator takes the byte count as an int. -CNA
        if (vcount > (0x7FFFFFFFu / 4))
            return 0;
        value->value_count = (unsigned int) vcount;
        const uint64 bytes = vcount * 4u;
        value->values = m((int) bytes, d);
        if (value->values == NULL)
            return 0;
        memset(value->values, '\0', (size_t) bytes);
        for (i = 0; i < (int) rows; i++)
            memcpy(value->valuesF + (i << 2), valptr + ((columncount << 2) * i), columncount << 2);
    } // if
    else if (valclass == MOJOSHADER_SYMCLASS_OBJECT)
    {
        /* This class contains either samplers or "objects" */
        if (type == MOJOSHADER_SYMTYPE_SAMPLER
         || type == MOJOSHADER_SYMTYPE_SAMPLER1D
         || type == MOJOSHADER_SYMTYPE_SAMPLER2D
         || type == MOJOSHADER_SYMTYPE_SAMPLER3D
         || type == MOJOSHADER_SYMTYPE_SAMPLERCUBE)
        {
            const uint32 numstates = readui32(&valptr, &vallen);

            // Each sampler state is four words of the value data; a count larger than what is
            // left cannot be honest and would overflow the allocation below. -CNA
            if ((base_len != 0) && (numstates > (vallen / (4 * sizeof (uint32)))))
                return 0;

            value->value_count = numstates;

            const uint64 siz = (uint64) sizeof (MOJOSHADER_effectSamplerState) * numstates;
            // The allocator takes the byte count as an int; memset below takes all of it. -CNA
            if (siz > 0x7FFFFFFFu)
                return 0;
            value->values = m((int) siz, d);
            if (value->values == NULL)
                return 0;
            memset(value->values, '\0', (size_t) siz);

            for (i = 0; i < (int) numstates; i++)
            {
                MOJOSHADER_effectSamplerState *state = &value->valuesSS[i];
                const uint32 stype = readui32(&valptr, &vallen) & ~0xA0;
                /*const uint32 FIXME =*/ readui32(&valptr, &vallen);
                const uint32 statetypeoffset = readui32(&valptr, &vallen);
                const uint32 statevaloffset = readui32(&valptr, &vallen);

                state->type = (MOJOSHADER_samplerStateType) stype;
                if (!readvalue(base, base_len, statetypeoffset, statevaloffset,
                               &state->value, objects, object_count, depth + 1,
                               m, d))
                    return 0;
                if (stype == MOJOSHADER_SAMP_TEXTURE)
                {
                    // The texture index is content; it selects an object whose type this writes,
                    // so it has to be present and in range. -CNA
                    if ((state->value.values == NULL) || (state->value.value_count < 1) ||
                        ((uint32) state->value.valuesI[0] >= object_count))
                        return 0;
                    objects[state->value.valuesI[0]].type = (MOJOSHADER_symbolType) type;
                } // if
            } // for
        } // if
        else
        {
            uint32 numobjects = 1;
            if (numelements > 0)
                numobjects = numelements;

            // One word per object, read from the value data; bound the count against it. -CNA
            if ((base_len != 0) && (numobjects > (vallen / sizeof (uint32))))
                return 0;

            value->value_count = numobjects;

            const uint32 siz = 4 * numobjects;
            value->values = m(siz, d);
            if (value->values == NULL)
                return 0;
            memcpy(value->values, valptr, siz);
            #if MOJOSHADER_BIG_ENDIAN
            int valI;
            for (valI=0;valI < (value->value_count);valI++) {
                value->valuesI[valI] = SWAP32(value->valuesI[valI]);
            }
            #endif
            for (i = 0; i < (int) value->value_count; i++)
            {
                // Each object reference selects an object whose type this writes. -CNA
                if ((uint32) value->valuesI[i] >= object_count)
                    return 0;
                objects[value->valuesI[i]].type = (MOJOSHADER_symbolType) type;
            } // for
        } // else
    } // else if
    else if (valclass == MOJOSHADER_SYMCLASS_STRUCT)
    {
        uint32 siz;

        value->type.member_count = readui32(&typeptr, &typelen);
        // The member count is content. Each member is seven words of type metadata, so a count
        // larger than what is left of the type data cannot be honest; the size multiplication
        // below would also wrap and hand back a buffer smaller than the loop that fills it.
        // Without the zeroing, the nested-struct fields this loop never assigns (see the FIXME
        // below) are read later as whatever the heap held. -CNA
        if ((value->type.member_count > (0xFFFFFFFFu / sizeof (MOJOSHADER_symbolStructMember))) ||
            ((base_len != 0) && (value->type.member_count > (typelen / (7 * sizeof (uint32))))))
        {
            value->type.member_count = 0;
            value->type.members = NULL;
            return 0;
        } // if
        siz = value->type.member_count * sizeof (MOJOSHADER_symbolStructMember);
        value->type.members = (MOJOSHADER_symbolStructMember *) m(siz, d);
        if (value->type.members == NULL)
        {
            value->type.member_count = 0;
            return 0;
        } // if
        memset(value->type.members, '\0', siz);

        uint64 structsize = 0;
        for (i = 0; i < (int) value->type.member_count; i++)
        {
            MOJOSHADER_symbolStructMember *mem = &value->type.members[i];

            mem->info.parameter_type = (MOJOSHADER_symbolType) readui32(&typeptr, &typelen);
            mem->info.parameter_class = (MOJOSHADER_symbolClass) readui32(&typeptr, &typelen);

            const uint32 memname = readui32(&typeptr, &typelen);
            /*const uint32 memsemantic =*/ readui32(&typeptr, &typelen);
            mem->name = readstring(base, base_len, memname, m, d);

            mem->info.elements = readui32(&typeptr, &typelen);
            mem->info.columns = readui32(&typeptr, &typelen);
            mem->info.rows = readui32(&typeptr, &typelen);

            // !!! FIXME: Nested structs! -flibit
            mem->info.member_count = 0;
            mem->info.members = NULL;

            // As for a plain value: each member row is copied into its own four-float register,
            // so a member wider than four would write past it. -CNA
            if ((mem->info.columns > 4) || (mem->info.rows > 4))
                return 0;

            uint64 memsize = (uint64) 4 * mem->info.rows;
            if (mem->info.elements > 0)
                memsize *= mem->info.elements;
            structsize += memsize;
        } // for

        if ((base_len != 0) && (structsize > base_len))
            return 0;
        // value_count * 4 below is the allocation size, an int for the allocator. -CNA
        if (structsize > (0x7FFFFFFFu / 4))
            return 0;
        value->type.columns = (unsigned int) structsize;
        value->type.rows = 1;
        uint64 vcount = structsize;
        if (numelements > 0)
            vcount *= numelements;
        if ((base_len != 0) && (vcount > base_len))
            return 0;
        if (vcount > (0x7FFFFFFFu / 4))
            return 0;
        value->value_count = (unsigned int) vcount;

        siz = value->value_count * 4;
        value->values = m(siz, d);
        if (value->values == NULL)
            return 0;
        memset(value->values, '\0', siz);
        // typeptr now sits just past the member metadata, where fxc packs the struct's defaults;
        // bound each copy against the base region and the destination buffer. -CNA
        uint32 typeptr_off = (uint32) (typeptr - base);
        int dst_offset = 0, src_offset = 0;
        i = 0;
        do
        {
            for (j = 0; j < (int) value->type.member_count; j++)
            {
                // A non-array member has elements == 0 and still holds one value, exactly as
                // the size computation above counts it; multiplying by 0 skipped its defaults
                // and shifted every later member's onto it. -CNA
                const uint32 elements = value->type.members[j].info.elements;
                const uint32 columns = value->type.members[j].info.columns;
                siz = value->type.members[j].info.rows * (elements ? elements : 1);
                for (k = 0; k < (int) siz; k++)
                {
                    // The destination register has to hold the copy whether or not the source
                    // span is known. -CNA
                    if ((uint64) dst_offset + 4 > value->value_count)
                        return 0;
                    if ((base_len != 0) &&
                        ((uint64) typeptr_off + src_offset + (columns << 2) > base_len))
                        return 0;
                    memcpy(value->valuesF + dst_offset,
                           typeptr + src_offset, /* Yes, typeptr. -flibit */
                           columns << 2);
                    dst_offset += 4;
                    src_offset += columns << 2;
                } // for
            }
        } while (++i < (int) numelements);
    } // else if
    else if (base_len != 0)
    {
        // A class the four cases above do not cover is not XNA content. Upstream asserted this
        // (and the per-class type ranges); untrusted content reaches it, so refuse. -CNA
        return 0;
    } // else if
    return 1;
} // readvalue

static int readannotations(const uint32 numannos,
                            const uint8 *base,
                            const uint32 base_len,
                            const uint8 **ptr,
                            uint32 *len,
                            MOJOSHADER_effectAnnotation **annotations,
                            MOJOSHADER_effectObject *objects,
                            const uint32 object_count,
                            MOJOSHADER_malloc m,
                            void *d)
{
    int i;
    if (numannos == 0) return 1;

    const uint32 siz = sizeof(MOJOSHADER_effectAnnotation) * numannos;
    *annotations = (MOJOSHADER_effectAnnotation *) m(siz, d);
    if (*annotations == NULL)
        return 0;
    memset(*annotations, '\0', siz);

    for (i = 0; i < numannos; i++)
    {
        MOJOSHADER_effectAnnotation *anno = &(*annotations)[i];

        const uint32 typeoffset = readui32(ptr, len);
        const uint32 valoffset = readui32(ptr, len);

        if (!readvalue(base, base_len, typeoffset, valoffset,
                       anno, objects, object_count, 0,
                       m, d))
            return 0;
    } // for
    return 1;
} // readannotation

static int readparameters(const uint32 numparams,
                           const uint8 *base,
                            const uint32 base_len,
                           const uint8 **ptr,
                           uint32 *len,
                           MOJOSHADER_effectParam **params,
                           MOJOSHADER_effectObject *objects,
                           const uint32 object_count,
                           MOJOSHADER_malloc m,
                           void *d)
{
    int i;
    if (numparams == 0) return 1;

    uint32 siz = sizeof(MOJOSHADER_effectParam) * numparams;
    *params = (MOJOSHADER_effectParam *) m(siz, d);
    if (*params == NULL)
        return 0;
    memset(*params, '\0', siz);

    for (i = 0; i < numparams; i++)
    {
        MOJOSHADER_effectParam *param = &(*params)[i];

        const uint32 typeoffset = readui32(ptr, len);
        const uint32 valoffset = readui32(ptr, len);
        /*const uint32 flags =*/ readui32(ptr, len);
        const uint32 numannos = readui32(ptr, len);

        param->annotation_count = numannos;
        if (!readannotations(numannos, base, base_len, ptr, len,
                             &param->annotations, objects, object_count,
                             m, d))
            return 0;

        if (!readvalue(base, base_len, typeoffset, valoffset,
                       &param->value, objects, object_count, 0,
                       m, d))
            return 0;
    } // for
    return 1;
} // readparameters

static int render_state_type_is_valid(const uint32 type)
{
    return (type <= MOJOSHADER_RS_BLENDOPALPHA) || (type == MOJOSHADER_RS_VERTEXSHADER)
        || (type == MOJOSHADER_RS_PIXELSHADER) || (type == MOJOSHADER_RS_PIXELSHADERCONSTANT)
        || (type == MOJOSHADER_RS_SETSAMPLER)
        || ((type >= MOJOSHADER_RS_BUMPENVMAT00) &&
            (type <= MOJOSHADER_RS_BUMPENVMAT11))
        || (type == MOJOSHADER_RS_BUMPENVLSCALE)
        || (type == MOJOSHADER_RS_BUMPENVLOFFSET);
} // render_state_type_is_valid

static int readstates(const uint32 numstates,
                       const uint8 *base,
                            const uint32 base_len,
                       const uint8 **ptr,
                       uint32 *len,
                       MOJOSHADER_effectState **states,
                       MOJOSHADER_effectObject *objects,
                       const uint32 object_count,
                       MOJOSHADER_malloc m,
                       void *d)
{
    int i;
    if (numstates == 0) return 1;

    const uint32 siz = sizeof (MOJOSHADER_effectState) * numstates;
    *states = (MOJOSHADER_effectState *) m(siz, d);
    if (*states == NULL)
        return 0;
    memset(*states, '\0', siz);

    for (i = 0; i < numstates; i++)
    {
        MOJOSHADER_effectState *state = &(*states)[i];

        const uint32 type = readui32(ptr, len);  /* already a render-state ABI identifier. */
        const uint32 target = readui32(ptr, len);
        const uint32 typeoffset = readui32(ptr, len);
        const uint32 valoffset = readui32(ptr, len);

        if (!render_state_type_is_valid(type))
            return 0;
        state->type = (MOJOSHADER_renderStateType) type;
        state->index = target;
        state->referenced_parameter = -1;
        if (!readvalue(base, base_len, typeoffset, valoffset,
                       &state->value, objects, object_count, 0,
                       m, d))
            return 0;
    } // for
    return 1;
} // readstates

static int readpasses(const uint32 numpasses,
                       const uint8 *base,
                            const uint32 base_len,
                       const uint8 **ptr,
                       uint32 *len,
                       MOJOSHADER_effectPass **passes,
                       MOJOSHADER_effectObject *objects,
                       const uint32 object_count,
                       MOJOSHADER_malloc m,
                       void *d)
{
    int i;
    if (numpasses == 0) return 1;

    const uint32 siz = sizeof (MOJOSHADER_effectPass) * numpasses;
    *passes = (MOJOSHADER_effectPass *) m(siz, d);
    if (*passes == NULL)
        return 0;
    memset(*passes, '\0', siz);

    for (i = 0; i < numpasses; i++)
    {
        MOJOSHADER_effectPass *pass = &(*passes)[i];

        const uint32 passnameoffset = readui32(ptr, len);
        const uint32 numannos = readui32(ptr, len);
        const uint32 numstates = readui32(ptr, len);

        pass->name = readstring(base, base_len, passnameoffset, m, d);

        pass->annotation_count = numannos;
        if (!readannotations(numannos, base, base_len, ptr, len,
                             &pass->annotations, objects, object_count,
                             m, d))
            return 0;

        pass->state_count = numstates;
        if (!readstates(numstates, base, base_len, ptr, len,
                        &pass->states, objects, object_count,
                        m, d))
            return 0;
    } // for
    return 1;
} // readpasses

static int readtechniques(const uint32 numtechniques,
                           const uint8 *base,
                            const uint32 base_len,
                           const uint8 **ptr,
                           uint32 *len,
                           MOJOSHADER_effectTechnique **techniques,
                           MOJOSHADER_effectObject *objects,
                           const uint32 object_count,
                           MOJOSHADER_malloc m,
                           void *d)
{
    int i;
    if (numtechniques == 0) return 1;

    const uint32 siz = sizeof (MOJOSHADER_effectTechnique) * numtechniques;
    *techniques = (MOJOSHADER_effectTechnique *) m(siz, d);
    if (*techniques == NULL)
        return 0;
    memset(*techniques, '\0', siz);

    for (i = 0; i < numtechniques; i++)
    {
        MOJOSHADER_effectTechnique *technique = &(*techniques)[i];

        const uint32 nameoffset = readui32(ptr, len);
        const uint32 numannos = readui32(ptr, len);
        const uint32 numpasses = readui32(ptr, len);

        technique->name = readstring(base, base_len, nameoffset, m, d);

        technique->annotation_count = numannos;
        if (!readannotations(numannos, base, base_len, ptr, len,
                             &technique->annotations, objects, object_count,
                             m, d))
            return 0;

        technique->pass_count = numpasses;
        if (!readpasses(numpasses, base, base_len, ptr, len,
                        &technique->passes, objects, object_count,
                        m, d))
            return 0;
    } // for
    return 1;
} // readtechniques

static void readsmallobjects(const uint32 numsmallobjects,
                             const uint8 **ptr,
                             uint32 *len,
                             MOJOSHADER_effect *effect,
                             const MOJOSHADER_swizzle *swiz,
                             const unsigned int swizcount,
                             const MOJOSHADER_samplerMap *smap,
                             const unsigned int smapcount,
                             ErrorList *errors)
{
    int i, j;
    MOJOSHADER_parseData *pd;
    MOJOSHADER_malloc m = effect->ctx.m;
    void *d = effect->ctx.malloc_data;

    if (numsmallobjects == 0) return;

    for (i = 1; i < numsmallobjects + 1; i++)
    {
        const uint32 index = readui32(ptr, len);
        const uint32 length = readui32(ptr, len);

        // The block length is content, and everything below trusts it: strings are copied by it,
        // shader bytecode is handed to the shader parser with it as the buffer size, and the
        // stream is advanced by it. A length past the end of the payload therefore walks the
        // shader parser off the buffer entirely. -CNA
        if ((length > *len) || (index >= (uint32) effect->object_count))
        {
            errorlist_add(errors, NULL, 0, "Effect object block is out of range.");
            return;
        } // if

        MOJOSHADER_effectObject *object = &effect->objects[index];
        if (object->type == MOJOSHADER_SYMTYPE_STRING)
        {
            if (length > 0)
            {
                char *str = (char *) m(length, d);
                if (str == NULL)
                {
                    errorlist_add(errors, NULL, 0, "Effect object allocation failed.");
                    return;
                } // if
                memcpy(str, *ptr, length);
                object->string.string = str;
            } // if
        } // if
        else if (object->type == MOJOSHADER_SYMTYPE_TEXTURE
              || object->type == MOJOSHADER_SYMTYPE_TEXTURE1D
              || object->type == MOJOSHADER_SYMTYPE_TEXTURE2D
              || object->type == MOJOSHADER_SYMTYPE_TEXTURE3D
              || object->type == MOJOSHADER_SYMTYPE_TEXTURECUBE
              || object->type == MOJOSHADER_SYMTYPE_SAMPLER
              || object->type == MOJOSHADER_SYMTYPE_SAMPLER1D
              || object->type == MOJOSHADER_SYMTYPE_SAMPLER2D
              || object->type == MOJOSHADER_SYMTYPE_SAMPLER3D
              || object->type == MOJOSHADER_SYMTYPE_SAMPLERCUBE)
        {
            if (length > 0)
            {
                char *str = (char *) m(length, d);
                if (str == NULL)
                {
                    errorlist_add(errors, NULL, 0, "Effect object allocation failed.");
                    return;
                } // if
                memcpy(str, *ptr, length);
                object->mapping.name = str;
            } // if
        } // else if
        else if (object->type == MOJOSHADER_SYMTYPE_PIXELSHADER
              || object->type == MOJOSHADER_SYMTYPE_VERTEXSHADER)
        {
            char mainfn[32];
            // A zero-length program tells MOJOSHADER_parse the size is unknown, which makes it
            // scan for an end token with no bound at all and walk off the payload. -CNA
            if (length == 0)
            {
                errorlist_add(errors, NULL, 0, "Effect shader object is empty.");
                return;
            } // if
            snprintf(mainfn, sizeof(mainfn), "ShaderFunction%u", (unsigned int) index);
            object->shader.technique = -1;
            object->shader.pass = -1;
            object->shader.shader = effect->ctx.compileShader(effect->ctx.shaderContext,
                                                              mainfn, *ptr, length,
                                                              swiz, swizcount,
                                                              smap, smapcount);
            if (object->shader.shader == NULL)
            {
                // Bail ASAP, so we can get the error to the application
                errorlist_add(errors, NULL, 0, effect->ctx.getError(effect->ctx.shaderContext));
                return;
            } // if
            pd = effect->ctx.getParseData(object->shader.shader);
            if (pd->error_count > 0)
            {
                // Bail ASAP, so we can get the error to the application
                push_errors(errors, pd->errors, pd->error_count);
                return;
            } // if

            for (j = 0; j < pd->symbol_count; j++)
                if (pd->symbols[j].register_set == MOJOSHADER_SYMREGSET_SAMPLER)
                    object->shader.sampler_count++;
            object->shader.param_count = pd->symbol_count;
            object->shader.params = (uint32 *) m(object->shader.param_count * sizeof (uint32), d);
            object->shader.samplers = (MOJOSHADER_samplerStateRegister *) m(object->shader.sampler_count * sizeof (MOJOSHADER_samplerStateRegister), d);
            if (((object->shader.param_count > 0) && (object->shader.params == NULL)) ||
                ((object->shader.sampler_count > 0) && (object->shader.samplers == NULL)))
            {
                errorlist_add(errors, NULL, 0, "Effect shader table allocation failed.");
                return;
            } // if
            uint32 curSampler = 0;
            for (j = 0; j < pd->symbol_count; j++)
            {
                int par = findparameter(effect->params,
                                        effect->param_count,
                                        pd->symbols[j].name,
                                        errors);
                if (par < 0)
                    return;
                object->shader.params[j] = par;
                if (pd->symbols[j].register_set == MOJOSHADER_SYMREGSET_SAMPLER)
                {
                    // The shader's constant table matched this sampler symbol to an effect
                    // parameter by name alone. If that parameter is not actually a sampler, its
                    // value storage is not an array of sampler states, and reinterpreting it as
                    // one reads far past the end. -CNA
                    if (!value_is_sampler(&effect->params[par].value))
                    {
                        errorlist_add(errors, NULL, 0,
                                      "Shader sampler names a parameter that is not a sampler.");
                        return;
                    } // if
                    object->shader.samplers[curSampler].sampler_name = effect->params[par].value.name;
                    object->shader.samplers[curSampler].sampler_register = pd->symbols[j].register_index;
                    object->shader.samplers[curSampler].sampler_state_count = effect->params[par].value.value_count;
                    object->shader.samplers[curSampler].sampler_states = effect->params[par].value.valuesSS;
                    curSampler++;
                } // if
            } // for
            if (pd->preshader)
            {
                object->shader.preshader_param_count = pd->preshader->symbol_count;
                object->shader.preshader_params = (uint32 *) m(object->shader.preshader_param_count * sizeof (uint32), d);
                if ((object->shader.preshader_param_count > 0) &&
                    (object->shader.preshader_params == NULL))
                {
                    errorlist_add(errors, NULL, 0, "Effect shader table allocation failed.");
                    return;
                } // if
                for (j = 0; j < pd->preshader->symbol_count; j++)
                {
                    int par = findparameter(effect->params,
                                            effect->param_count,
                                            pd->preshader->symbols[j].name,
                                            errors);
                    if (par < 0)
                        return;
                    object->shader.preshader_params[j] = par;
                } // for
            } // if
        } // else if
        else
        {
            assert(0 && "Small object type unknown!");
        } // else

        /* Object block is always a multiple of four */
        // The unpadded length was bounded against *len above, but the stream advances by the
        // padded length; padding it up can push it past the end, so the padded length is bounded
        // too (as readlargeobjects already does). -CNA
        if (length > 0xFFFFFFFCu)
        {
            errorlist_add(errors, NULL, 0, "Effect object block padding overflows.");
            return;
        } // if
        const uint32 blocklen = (length + 3u) & ~3u;
        if (blocklen > *len)
        {
            errorlist_add(errors, NULL, 0, "Effect object block padding is out of range.");
            return;
        } // if
        *ptr += blocklen;
        *len -= blocklen;
    } // for
} // readstrings

static void readlargeobjects(const uint32 numlargeobjects,
                             const uint32 numsmallobjects,
                             const uint8 **ptr,
                             uint32 *len,
                             MOJOSHADER_effect *effect,
                             const MOJOSHADER_swizzle *swiz,
                             const unsigned int swizcount,
                             const MOJOSHADER_samplerMap *smap,
                             const unsigned int smapcount,
                             ErrorList *errors)
{
    int i, j;
    MOJOSHADER_parseData *pd;
    MOJOSHADER_malloc m = effect->ctx.m;
    MOJOSHADER_free f = effect->ctx.f;
    void *d = effect->ctx.malloc_data;

    if (numlargeobjects == 0) return;

    int numobjects = numsmallobjects + numlargeobjects + 1;
    for (i = numsmallobjects + 1; i < numobjects; i++)
    {
        const uint32 technique = readui32(ptr, len);
        const uint32 index = readui32(ptr, len);
        /*const uint32 FIXME =*/ readui32(ptr, len);
        const uint32 state = readui32(ptr, len);
        const uint32 type = readui32(ptr, len);
        const uint32 length = readui32(ptr, len);

        // Same reasoning as readsmallobjects: an unbounded block length hands the shader parser
        // a buffer size that outruns the payload. -CNA
        if (length > *len)
        {
            errorlist_add(errors, NULL, 0, "Effect object block is out of range.");
            return;
        } // if

        if (length > 0xFFFFFFFCu)
        {
            errorlist_add(errors, NULL, 0, "Effect object block padding overflows.");
            return;
        } // if
        const uint32 blocklen = (length + 3u) & ~3u;
        if (blocklen > *len)
        {
            errorlist_add(errors, NULL, 0, "Effect object block padding is out of range.");
            return;
        } // if

        // Every index in this chain is content, and each one selects the array the next one
        // indexes. Official XNA4 output uses these auxiliary blocks for references from pass
        // sampler assignments and for preshader expressions that compute pass constants. -CNA
        uint32 objectIndex;
        MOJOSHADER_effectState *passState = NULL;
        MOJOSHADER_effectValue *value;
        if (technique == -1)
        {
            if ((index >= (uint32) effect->param_count)
             || (!value_is_sampler(&effect->params[index].value))
             || (state >= (uint32) effect->params[index].value.value_count))
            {
                errorlist_add(errors, NULL, 0, "Effect object names no sampler state.");
                return;
            } // if
            value = &effect->params[index].value.valuesSS[state].value;
        } // if
        else
        {
            if ((technique >= (uint32) effect->technique_count)
             || (index >= (uint32) effect->techniques[technique].pass_count)
             || (state >= (uint32) effect->techniques[technique].passes[index].state_count))
            {
                errorlist_add(errors, NULL, 0, "Effect object names no pass state.");
                return;
            } // if
            passState = &effect->techniques[technique].passes[index].states[state];
            value = &passState->value;

            if (type == 1)
            {
                int par;
                if ((length == 0) || (memchr(*ptr, '\0', length) == NULL))
                {
                    errorlist_add(errors, NULL, 0,
                                  "Effect parameter reference is not a bounded string.");
                    return;
                } // if
                par = findparameter(effect->params, effect->param_count,
                                    (const char *) *ptr, errors);
                if (par < 0)
                    return;
                passState->referenced_parameter = par;
            } // if

            if ((passState->type == MOJOSHADER_RS_PIXELSHADERCONSTANT) && (type == 0))
            {
                if (length == 0)
                {
                    errorlist_add(errors, NULL, 0,
                                  "Effect constant expression is empty.");
                    return;
                } // if
                passState->preshader = (MOJOSHADER_preshader *)
                    MOJOSHADER_parsePreshader(*ptr, length, m, f, d);
                if (passState->preshader == NULL)
                {
                    errorlist_add(errors, NULL, 0,
                                  "Effect constant expression could not be parsed.");
                    return;
                } // if
                passState->preshader_param_count = passState->preshader->symbol_count;
                if (passState->preshader_param_count > 0)
                {
                    uint32 parameterIndex;
                    if (passState->preshader_param_count >
                        (0x7FFFFFFFu / sizeof (uint32)))
                    {
                        errorlist_add(errors, NULL, 0,
                                      "Effect constant expression has too many parameters.");
                        return;
                    } // if
                    passState->preshader_params = (uint32 *) m(
                        passState->preshader_param_count * sizeof (uint32), d);
                    if (passState->preshader_params == NULL)
                    {
                        errorlist_add(errors, NULL, 0,
                                      "Effect constant expression parameter allocation failed.");
                        return;
                    } // if
                    for (parameterIndex = 0;
                         parameterIndex < passState->preshader_param_count;
                         parameterIndex++)
                    {
                        const int par = findparameter(
                            effect->params, effect->param_count,
                            passState->preshader->symbols[parameterIndex].name,
                            errors);
                        if (par < 0)
                            return;
                        passState->preshader_params[parameterIndex] = (uint32) par;
                    } // for
                } // if
                *ptr += blocklen;
                *len -= blocklen;
                continue;
            } // if
        } // else

        if (value->value_count < 1)
        {
            if (type != 1)
            {
                errorlist_add(errors, NULL, 0,
                              passState != NULL ? "Effect object names an empty pass state."
                                                : "Effect object names an empty sampler state.");
                return;
            } // if
            *ptr += blocklen;
            *len -= blocklen;
            continue;
        } // if
        objectIndex = value->valuesI[0];

        if (objectIndex >= (uint32) effect->object_count)
        {
            errorlist_add(errors, NULL, 0, "Effect object index is out of range.");
            return;
        } // if

        MOJOSHADER_effectObject *object = &effect->objects[objectIndex];
        if (object->type == MOJOSHADER_SYMTYPE_PIXELSHADER
         || object->type == MOJOSHADER_SYMTYPE_VERTEXSHADER)
        {
            object->shader.technique = technique;
            object->shader.pass = index;

            if (type == 2)
            {
                /* This is a standalone preshader!
                 * It exists solely for effect passes that do not use a single
                 * vertex/fragment shader.
                 */
                object->shader.is_preshader = 1;
                // The block opens with the offset of the preshader data within it; the preshader
                // then runs from there to the block's end. Reading that word needs four bytes,
                // and the preshader must be handed the bytes that remain after the offset, not
                // the whole block length -- which ran its parse past the block. -CNA
                if (length < sizeof (uint32))
                {
                    errorlist_add(errors, NULL, 0, "Effect preshader block is too small.");
                    return;
                } // if
                uint32 startword;
                memcpy(&startword, *ptr, sizeof (startword));
                const uint32 start = startword + 4;
                if ((startword > length) || (start > length))
                {
                    errorlist_add(errors, NULL, 0, "Effect preshader offset is out of range.");
                    return;
                } // if
                const char *array = readstring(*ptr, length, 0, m, d);
                object->shader.param_count = 1;
                object->shader.params = (uint32 *) m(sizeof (uint32), d);
                if (object->shader.params == NULL)
                {
                    f((void *) array, d);
                    return;
                } // if
                int par = findparameter(effect->params,
                                        effect->param_count,
                                        array,
                                        errors);
                f((void *) array, d);
                if (par < 0)
                    return;
                object->shader.params[0] = par;
                object->shader.preshader = MOJOSHADER_parsePreshader(*ptr + start, length - start,
                                                                     m, f, d);
                if (object->shader.preshader == NULL)
                {
                    errorlist_add(errors, NULL, 0,
                                  "Effect preshader could not be parsed.");
                    return;
                } // if
                object->shader.preshader_param_count = object->shader.preshader->symbol_count;
                object->shader.preshader_params = (uint32 *) m(object->shader.preshader_param_count * sizeof (uint32), d);
                if ((object->shader.preshader_param_count > 0) &&
                    (object->shader.preshader_params == NULL))
                {
                    errorlist_add(errors, NULL, 0, "Effect shader table allocation failed.");
                    return;
                } // if
                for (j = 0; j < object->shader.preshader->symbol_count; j++)
                {
                    int par = findparameter(effect->params,
                                            effect->param_count,
                                            object->shader.preshader->symbols[j].name,
                                            errors);
                    if (par < 0)
                        return;
                    object->shader.preshader_params[j] = par;
                } // for
            } // if
            else
            {
                char mainfn[32];
                // Same as readsmallobjects: an empty program means "size unknown" to the shader
                // parser, and it then reads past the effect payload looking for an end token.
                if (length == 0)
                {
                    errorlist_add(errors, NULL, 0, "Effect shader object is empty.");
                    return;
                } // if
                snprintf(mainfn, sizeof (mainfn), "ShaderFunction%u", (unsigned int) objectIndex);
                object->shader.shader = effect->ctx.compileShader(effect->ctx.shaderContext,
                                                                  mainfn, *ptr, length,
                                                                  swiz, swizcount,
                                                                  smap, smapcount);
                if (object->shader.shader == NULL)
                {
                    // Bail ASAP, so we can get the error to the application
                    errorlist_add(errors, NULL, 0, effect->ctx.getError(effect->ctx.shaderContext));
                    return;
                } // if
                pd = effect->ctx.getParseData(object->shader.shader);
                if (pd->error_count > 0)
                {
                    // Bail ASAP, so we can get the error to the application
                    push_errors(errors, pd->errors, pd->error_count);
                    return;
                } // if

                for (j = 0; j < pd->symbol_count; j++)
                    if (pd->symbols[j].register_set == MOJOSHADER_SYMREGSET_SAMPLER)
                        object->shader.sampler_count++;
                object->shader.param_count = pd->symbol_count;
                object->shader.params = (uint32 *) m(object->shader.param_count * sizeof (uint32), d);
                object->shader.samplers = (MOJOSHADER_samplerStateRegister *) m(object->shader.sampler_count * sizeof (MOJOSHADER_samplerStateRegister), d);
                if (((object->shader.param_count > 0) && (object->shader.params == NULL)) ||
                    ((object->shader.sampler_count > 0) && (object->shader.samplers == NULL)))
                {
                    errorlist_add(errors, NULL, 0, "Effect shader table allocation failed.");
                    return;
                } // if
                uint32 curSampler = 0;
                for (j = 0; j < pd->symbol_count; j++)
                {
                    int par = findparameter(effect->params,
                                            effect->param_count,
                                            pd->symbols[j].name,
                                            errors);
                    if (par < 0)
                        return;
                    object->shader.params[j] = par;
                    if (pd->symbols[j].register_set == MOJOSHADER_SYMREGSET_SAMPLER)
                    {
                        // The shader's constant table matched this sampler symbol to an effect
                        // parameter by name alone. If that parameter is not actually a sampler, its
                        // value storage is not an array of sampler states, and reinterpreting it as
                        // one reads far past the end. -CNA
                        if ((effect->params[par].value.type.parameter_type <
                             MOJOSHADER_SYMTYPE_SAMPLER) ||
                            (effect->params[par].value.type.parameter_type >
                             MOJOSHADER_SYMTYPE_SAMPLERCUBE))
                        {
                            errorlist_add(errors, NULL, 0,
                                          "Shader sampler names a parameter that is not a sampler.");
                            return;
                        } // if
                        object->shader.samplers[curSampler].sampler_name = effect->params[par].value.name;
                        object->shader.samplers[curSampler].sampler_register = pd->symbols[j].register_index;
                        object->shader.samplers[curSampler].sampler_state_count = effect->params[par].value.value_count;
                        object->shader.samplers[curSampler].sampler_states = effect->params[par].value.valuesSS;
                        curSampler++;
                    } // if
                } // for
                if (pd->preshader)
                {
                    object->shader.preshader_param_count = pd->preshader->symbol_count;
                    object->shader.preshader_params = (uint32 *) m(object->shader.preshader_param_count * sizeof (uint32), d);
                    if ((object->shader.preshader_param_count > 0) &&
                        (object->shader.preshader_params == NULL))
                    {
                        errorlist_add(errors, NULL, 0, "Effect shader table allocation failed.");
                        return;
                    } // if
                    for (j = 0; j < pd->preshader->symbol_count; j++)
                    {
                        int par = findparameter(effect->params,
                                                effect->param_count,
                                                pd->preshader->symbols[j].name,
                                                errors);
                        if (par < 0)
                            return;
                        object->shader.preshader_params[j] = par;
                    } // for
                } // if
            }
        } // if
        else if (object->type == MOJOSHADER_SYMTYPE_TEXTURE
              || object->type == MOJOSHADER_SYMTYPE_TEXTURE1D
              || object->type == MOJOSHADER_SYMTYPE_TEXTURE2D
              || object->type == MOJOSHADER_SYMTYPE_TEXTURE3D
              || object->type == MOJOSHADER_SYMTYPE_TEXTURECUBE
              || object->type == MOJOSHADER_SYMTYPE_SAMPLER
              || object->type == MOJOSHADER_SYMTYPE_SAMPLER1D
              || object->type == MOJOSHADER_SYMTYPE_SAMPLER2D
              || object->type == MOJOSHADER_SYMTYPE_SAMPLER3D
              || object->type == MOJOSHADER_SYMTYPE_SAMPLERCUBE)
        {
            if (length > 0)
            {
                char *str = (char *) m(length, d);
                if (str == NULL)
                {
                    errorlist_add(errors, NULL, 0, "Effect object allocation failed.");
                    return;
                } // if
                memcpy(str, *ptr, length);
                object->mapping.name = str;
            } // if
        } // else if
        else if (object->type != MOJOSHADER_SYMTYPE_VOID) // FIXME: Why? -flibit
        {
            assert(0 && "Large object type unknown!");
        } // else

        /* Object block is always a multiple of four */
        *ptr += blocklen;
        *len -= blocklen;
    } // for
} // readobjects

MOJOSHADER_effect *MOJOSHADER_compileEffect(const unsigned char *buf,
                                            const unsigned int _len,
                                            const MOJOSHADER_swizzle *swiz,
                                            const unsigned int swizcount,
                                            const MOJOSHADER_samplerMap *smap,
                                            const unsigned int smapcount,
                                            const MOJOSHADER_effectShaderContext *ctx)
{
    const uint8 *ptr = (const uint8 *) buf;
    uint32 len = (uint32) _len;
    ErrorList *errors;
    MOJOSHADER_malloc m;
    MOJOSHADER_free f;
    void *d;

    /* Need a backend! */
    if (ctx == NULL)
        return &MOJOSHADER_need_a_backend_effect;

    /* Supply both m and f, or neither */
    if ( ((ctx->m == NULL) && (ctx->f != NULL))
      || ((ctx->m != NULL) && (ctx->f == NULL)) )
        return &MOJOSHADER_out_of_mem_effect;

    /* Use default malloc/free if m/f were not passed */
    if (ctx->m == NULL)
        m = MOJOSHADER_internal_malloc;
    else
        m = ctx->m;
    if (ctx->f == NULL)
        f = MOJOSHADER_internal_free;
    else
        f = ctx->f;
    d = ctx->malloc_data;

    /* malloc base effect structure */
    MOJOSHADER_effect *retval = (MOJOSHADER_effect *) m(sizeof (MOJOSHADER_effect),
                                                        ctx->malloc_data);
    if (retval == NULL)
        return &MOJOSHADER_out_of_mem_effect;
    memset(retval, '\0', sizeof (*retval));

    /* Store ctx in effect structure */
    memcpy(&retval->ctx, ctx, sizeof(MOJOSHADER_effectShaderContext));
    retval->ctx.m = m;
    retval->ctx.f = f;

    if (len < 8)
        goto parseEffect_unexpectedEOF;

    /* Read in header magic, seek to initial offset */
    const uint8 *base = NULL;
    uint32 base_len = 0;
    uint16 magic;
    uint8 version_major;
    uint8 version_minor;
    read_version_token(&ptr, &len, &magic, &version_major, &version_minor);
    if ((magic == 0xBCF0) && (version_major == 0x0B) && (version_minor == 0xCF))
    {
        /* The Effect compiler provided with XNA4 adds some extra mess at the
         * beginning of the file. It's useless though, so just skip it.
         * -flibit
         */
        const uint32 skip = readui32(&ptr, &len) - 8;
        // Skipping forward has to shorten what is left, not lengthen it. Adding here inflated the
        // remaining-length counter every later bound in this parser is measured against -- and
        // every real XNA4-wrapped effect takes this branch. -CNA
        if (skip > len)
            goto parseEffect_unexpectedEOF;
        ptr += skip;
        len -= skip;
        // read_version_token() reads a four-byte token unconditionally; after the skip there may
        // be fewer than four bytes left. -CNA
        if (len < sizeof (uint32))
            goto parseEffect_unexpectedEOF;
        read_version_token(&ptr, &len, &magic, &version_major, &version_minor);
    } // if
    if (!((magic == 0xFEFF) && (version_major == 0x09) && (version_minor == 0x01)))
    {
        MOJOSHADER_deleteEffect(retval);
        return &MOJOSHADER_not_an_effect_effect;
    } // if
    else
    {
        const uint32 offset = readui32(&ptr, &len);
        base = ptr;
        if (offset > len)
            goto parseEffect_unexpectedEOF;
        // Every string in the file is addressed as an offset from base, so the parser needs to
        // know how many bytes actually follow base to bound those reads. -CNA
        base_len = len;
        ptr += offset;
        len -= offset;
    } // else

    if (len < 16)
        goto parseEffect_unexpectedEOF;

    /* Parse structure counts */
    const uint32 numparams = readui32(&ptr, &len);
    const uint32 numtechniques = readui32(&ptr, &len);
    /*const uint32 FIXME =*/ readui32(&ptr, &len);
    const uint32 numobjects = readui32(&ptr, &len);

    /* Every one of these counts is chosen by the file and is then used both to size an
     * allocation and to bound a write loop. The smallest record any of them can describe is two
     * 32-bit words, so a count larger than a quarter of what is left cannot be honest -- and
     * without this check the size multiplications below overflow and hand back a buffer far
     * smaller than the loops that fill it. -CNA
     */
    if ((numparams > (len / 8)) || (numtechniques > (len / 8)) || (numobjects > (len / 8)))
        goto parseEffect_unexpectedEOF;
    if (numobjects > (0xFFFFFFFFu / sizeof (MOJOSHADER_effectObject)))
        goto parseEffect_unexpectedEOF;

    /* Alloc structures now, so object types can be stored */
    retval->object_count = numobjects;
    const uint32 siz = sizeof (MOJOSHADER_effectObject) * numobjects;
    retval->objects = (MOJOSHADER_effectObject *) m(siz, d);
    if (retval->objects == NULL)
        goto parseEffect_outOfMemory;
    memset(retval->objects, '\0', siz);

    /* Parse effect parameters */
    retval->param_count = numparams;
    if (!readparameters(numparams, base, base_len, &ptr, &len,
                        &retval->params, retval->objects, numobjects,
                        m, d))
    {
        MOJOSHADER_deleteEffect(retval);
        return &MOJOSHADER_not_an_effect_effect;
    } // if

    /* Parse effect techniques */
    retval->technique_count = numtechniques;
    if (!readtechniques(numtechniques, base, base_len, &ptr, &len,
                        &retval->techniques, retval->objects, numobjects,
                        m, d))
    {
        MOJOSHADER_deleteEffect(retval);
        return &MOJOSHADER_not_an_effect_effect;
    } // if

    /* Initial effect technique/pass */
    retval->current_technique = &retval->techniques[0];
    retval->current_pass = -1;

    if (len < 8)
        goto parseEffect_unexpectedEOF;

    /* Parse object counts */
    const int numsmallobjects = readui32(&ptr, &len);
    const int numlargeobjects = readui32(&ptr, &len);

    errors = errorlist_create(m, f, d);
    if (errors == NULL)
        goto parseEffect_outOfMemory;

    /* Parse "small" object table */
    readsmallobjects(numsmallobjects, &ptr, &len, retval,
                     swiz, swizcount, smap, smapcount, errors);
    if (errorlist_count(errors) == 0)
    {
        /* Parse "large" object table. */
        readlargeobjects(numlargeobjects, numsmallobjects, &ptr, &len, retval,
                         swiz, swizcount, smap, smapcount, errors);
    } // if

    retval->error_count = errorlist_count(errors);
    retval->errors = errorlist_flatten(errors);
    errorlist_destroy(errors);

    return retval;

parseEffect_unexpectedEOF:
    MOJOSHADER_deleteEffect(retval);
    return &MOJOSHADER_unexpected_eof_effect;
parseEffect_outOfMemory:
    MOJOSHADER_deleteEffect(retval);
    return &MOJOSHADER_out_of_mem_effect;
} // MOJOSHADER_parseEffect


void freetypeinfo(MOJOSHADER_symbolTypeInfo *typeinfo,
                  MOJOSHADER_free f, void *d)
{
    int i;
    // A failed allocation can leave member_count nonzero with no member array; walk it only when
    // it exists, so a partially built type frees cleanly. -CNA
    if (typeinfo->members != NULL)
        for (i = 0; i < typeinfo->member_count; i++)
        {
            f((void *) typeinfo->members[i].name, d);
            freetypeinfo(&typeinfo->members[i].info, f, d);
        } // for
    f((void *) typeinfo->members, d);
} // freetypeinfo


void freevalue(MOJOSHADER_effectValue *value, MOJOSHADER_free f, void *d)
{
    int i;
    f((void *) value->name, d);
    f((void *) value->semantic, d);
    freetypeinfo(&value->type, f, d);
    // This has to match how the storage was allocated, not just the type: a value whose class is
    // not OBJECT never held sampler states, and walking it as if it did frees whatever the heap
    // held past the end of a much smaller buffer. The NULL check also covers a sampler value
    // whose state array a failed allocation never created. -CNA
    if (value_is_sampler(value) && (value->values != NULL))
        for (i = 0; i < (int) value->value_count; i++)
            freevalue(&value->valuesSS[i].value, f, d);
    f(value->values, d);
} // freevalue


void MOJOSHADER_deleteEffect(const MOJOSHADER_effect *_effect)
{
    MOJOSHADER_effect *effect = (MOJOSHADER_effect *) _effect;
    if ((effect == NULL) || effect_is_static_error(effect))
        return;  // no-op.

    MOJOSHADER_free f = effect->ctx.f;
    void *d = effect->ctx.malloc_data;
    int i, j, k;

    // Every array below may be NULL with a nonzero count when a parse or clone failed partway;
    // each loop is guarded on its array so a half-built effect frees without dereferencing one of
    // them. -CNA

    /* Free errors */
    if (effect->errors != NULL)
        for (i = 0; i < effect->error_count; i++)
        {
            f((void *) effect->errors[i].error, d);
            f((void *) effect->errors[i].filename, d);
        } // for
    f((void *) effect->errors, d);

    /* Free parameters, including annotations */
    if (effect->params != NULL)
        for (i = 0; i < effect->param_count; i++)
        {
            MOJOSHADER_effectParam *param = &effect->params[i];
            freevalue(&param->value, f, d);
            if (param->annotations != NULL)
                for (j = 0; j < param->annotation_count; j++)
                    freevalue(&param->annotations[j], f, d);
            f((void *) param->annotations, d);
        } // for
    f((void *) effect->params, d);

    /* Free techniques, including passes and all annotations */
    if (effect->techniques != NULL)
        for (i = 0; i < effect->technique_count; i++)
        {
            MOJOSHADER_effectTechnique *technique = &effect->techniques[i];
            f((void *) technique->name, d);
            if (technique->passes != NULL)
                for (j = 0; j < technique->pass_count; j++)
                {
                    MOJOSHADER_effectPass *pass = &technique->passes[j];
                    f((void *) pass->name, d);
                    if (pass->states != NULL)
                        for (k = 0; k < pass->state_count; k++)
                        {
                            MOJOSHADER_freePreshader(pass->states[k].preshader);
                            f((void *) pass->states[k].preshader_params, d);
                            freevalue(&pass->states[k].value, f, d);
                        } // for
                    f((void *) pass->states, d);
                    if (pass->annotations != NULL)
                        for (k = 0; k < pass->annotation_count; k++)
                            freevalue(&pass->annotations[k], f, d);
                    f((void *) pass->annotations, d);
                } // for
            f((void *) technique->passes, d);
            if (technique->annotations != NULL)
                for (j = 0; j < technique->annotation_count; j++)
                    freevalue(&technique->annotations[j], f, d);
            f((void *) technique->annotations, d);
        } // for
    f((void *) effect->techniques, d);

    /* Free object table */
    if (effect->objects != NULL)
    for (i = 0; i < effect->object_count; i++)
    {
        MOJOSHADER_effectObject *object = &effect->objects[i];
        if (object->type == MOJOSHADER_SYMTYPE_PIXELSHADER
         || object->type == MOJOSHADER_SYMTYPE_VERTEXSHADER)
        {
            if (object->shader.is_preshader)
                MOJOSHADER_freePreshader(object->shader.preshader);
            else
                effect->ctx.deleteShader(effect->ctx.shaderContext, object->shader.shader);
            f((void *) object->shader.params, d);
            f((void *) object->shader.samplers, d);
            f((void *) object->shader.preshader_params, d);
        } // if
        else if (object->type == MOJOSHADER_SYMTYPE_SAMPLER
              || object->type == MOJOSHADER_SYMTYPE_SAMPLER1D
              || object->type == MOJOSHADER_SYMTYPE_SAMPLER2D
              || object->type == MOJOSHADER_SYMTYPE_SAMPLER3D
              || object->type == MOJOSHADER_SYMTYPE_SAMPLERCUBE)
            f((void *) object->mapping.name, d);
        else if (object->type == MOJOSHADER_SYMTYPE_STRING)
            f((void *) object->string.string, d);
    } // for
    f((void *) effect->objects, d);

    /* Free base effect structure */
    f((void *) effect, d);
} // MOJOSHADER_freeEffect


// The copy helpers below return 0 when an allocation failed, leaving the destination partial but
// safe to free, and the clone gives up: a copy that quietly skipped a string or a value array
// (as these did) handed back a "successful" clone missing a parameter's name or its data. -CNA
#define COPY_STRING(location) \
    if (src->location != NULL) \
    { \
        siz = strlen(src->location) + 1; \
        stringcopy = (char *) m(siz, d); \
        if (stringcopy == NULL) \
            return 0; \
        strcpy(stringcopy, src->location); \
        dst->location = stringcopy; \
    } // if


int copysymboltypeinfo(MOJOSHADER_symbolTypeInfo *dst,
                       MOJOSHADER_symbolTypeInfo *src,
                       MOJOSHADER_malloc m,
                       void *d)
{
    int i;
    uint32 siz = 0;
    char *stringcopy = NULL;
    memcpy(dst, src, sizeof (MOJOSHADER_symbolTypeInfo));
    if (dst->member_count > 0)
    {
        siz = dst->member_count * sizeof (MOJOSHADER_symbolStructMember);
        dst->members = (MOJOSHADER_symbolStructMember *) m(siz, d);
        if (dst->members == NULL)
        {
            dst->member_count = 0;
            return 0;
        } // if
        // COPY_STRING assigns nothing when the source string is NULL, which a member name
        // legitimately is, so without this the copy keeps whatever the heap held there. -CNA
        memset(dst->members, '\0', siz);
        for (i = 0; i < dst->member_count; i++)
        {
            COPY_STRING(members[i].name)
            if (!copysymboltypeinfo(&dst->members[i].info, &src->members[i].info, m, d))
                return 0;
        } // for
    } // if
    return 1;
} // copysymboltypeinfo


int copyvalue(MOJOSHADER_effectValue *dst,
              MOJOSHADER_effectValue *src,
              MOJOSHADER_malloc m,
              void *d)
{
    int i;
    uint32 siz = 0;
    char *stringcopy = NULL;

    COPY_STRING(name)
    COPY_STRING(semantic)
    if (!copysymboltypeinfo(&dst->type, &src->type, m, d))
        return 0;
    dst->value_count = src->value_count;

    if (dst->type.parameter_class == MOJOSHADER_SYMCLASS_SCALAR
     || dst->type.parameter_class == MOJOSHADER_SYMCLASS_VECTOR
     || dst->type.parameter_class == MOJOSHADER_SYMCLASS_MATRIX_ROWS
     || dst->type.parameter_class == MOJOSHADER_SYMCLASS_MATRIX_COLUMNS
     || dst->type.parameter_class == MOJOSHADER_SYMCLASS_STRUCT)
    {
        siz = dst->value_count * 4;
        dst->values = m(siz, d);
        // A failed copy clears value_count so freevalue frees only what exists; memcpy also skips
        // a zero-length copy, whose source may legitimately be NULL. -CNA
        if (dst->values == NULL)
        {
            dst->value_count = 0;
            return 0;
        } // if
        if (siz > 0)
            memcpy(dst->values, src->values, siz);
    } // if
    else if (dst->type.parameter_class == MOJOSHADER_SYMCLASS_OBJECT)
    {
        if (dst->type.parameter_type == MOJOSHADER_SYMTYPE_SAMPLER
         || dst->type.parameter_type == MOJOSHADER_SYMTYPE_SAMPLER1D
         || dst->type.parameter_type == MOJOSHADER_SYMTYPE_SAMPLER2D
         || dst->type.parameter_type == MOJOSHADER_SYMTYPE_SAMPLER3D
         || dst->type.parameter_type == MOJOSHADER_SYMTYPE_SAMPLERCUBE)
        {
            siz = dst->value_count * sizeof (MOJOSHADER_effectSamplerState);
            dst->values = m(siz, d);
            // Clearing value_count before returning keeps freevalue from walking the sampler
            // states this failed allocation never created. -CNA
            if (dst->values == NULL)
            {
                dst->value_count = 0;
                return 0;
            } // if
            memset(dst->values, '\0', siz);
            for (i = 0; i < (int) dst->value_count; i++)
            {
                dst->valuesSS[i].type = src->valuesSS[i].type;
                if (!copyvalue(&dst->valuesSS[i].value,
                               &src->valuesSS[i].value,
                               m, d))
                    return 0;
            } // for
        } // if
        else
        {
            siz = dst->value_count * 4;
            dst->values = m(siz, d);
            if (dst->values == NULL)
            {
                dst->value_count = 0;
                return 0;
            } // if
            if (siz > 0)
                memcpy(dst->values, src->values, siz);
        } // else
    } // else if

    return 1;
} // copyvalue


#undef COPY_STRING


int copysymbolinfo(MOJOSHADER_symbolTypeInfo *dst,
                   MOJOSHADER_symbolTypeInfo *src,
                   MOJOSHADER_malloc m,
                   void *d)
{
    int i;
    uint32 siz;
    char *stringcopy;

    dst->parameter_class = src->parameter_class;
    dst->parameter_type = src->parameter_type;
    dst->rows = src->rows;
    dst->columns = src->columns;
    dst->elements = src->elements;
    dst->member_count = src->member_count;

    if (dst->member_count > 0)
    {
        siz = sizeof (MOJOSHADER_symbolStructMember) * dst->member_count;
        dst->members = (MOJOSHADER_symbolStructMember *) m(siz, d);
        if (dst->members == NULL)
        {
            dst->member_count = 0;
            return 0;
        } // if
        // Zero first, so a name or nested member a failed allocation below never reaches is a
        // NULL the destructor can free, not heap garbage it would dereference. -CNA
        memset(dst->members, '\0', siz);
        for (i = 0; i < (int) dst->member_count; i++)
        {
            if (src->members[i].name != NULL)
            {
                siz = strlen(src->members[i].name) + 1;
                stringcopy = (char *) m(siz, d);
                if (stringcopy == NULL)
                    return 0;
                strcpy(stringcopy, src->members[i].name);
                dst->members[i].name = stringcopy;
            } // if
            if (!copysymbolinfo(&dst->members[i].info, &src->members[i].info, m, d))
                return 0;
        } // for
    } // if
    return 1;
} // copysymbolinfo


int copysymbol(MOJOSHADER_symbol *dst,
               MOJOSHADER_symbol *src,
               MOJOSHADER_malloc m,
               void *d)
{
    uint32 siz = strlen(src->name) + 1;
    char *stringcopy = (char *) m(siz, d);
    // A failed name copy leaves dst->name NULL, which free_symbols tolerates. -CNA
    if (stringcopy != NULL)
        strcpy(stringcopy, src->name);
    dst->name = stringcopy;
    dst->register_set = src->register_set;
    dst->register_index = src->register_index;
    dst->register_count = src->register_count;
    if (stringcopy == NULL)
        return 0;
    return copysymbolinfo(&dst->info, &src->info, m, d);
} // copysymbol


MOJOSHADER_preshader *copypreshader(const MOJOSHADER_preshader *src,
                                    MOJOSHADER_malloc m,
                                    MOJOSHADER_free f,
                                    void *d)
{
    int i, j;
    uint32 siz;
    MOJOSHADER_preshader *retval;

    // Every count is published only once the array behind it exists, and every failed allocation
    // frees the partial preshader (MOJOSHADER_freePreshader walks the counts, which is why they
    // trail their arrays) and returns NULL. -CNA
    retval = (MOJOSHADER_preshader *) m(sizeof (MOJOSHADER_preshader), d);
    if (retval == NULL)
        return NULL;
    memset(retval, '\0', sizeof (MOJOSHADER_preshader));
    retval->malloc = m;
    retval->free = f;
    retval->malloc_data = d;

    siz = sizeof (double) * src->literal_count;
    retval->literals = (double *) m(siz, d);
    if (retval->literals == NULL)
    {
        MOJOSHADER_freePreshader(retval);
        return NULL;
    } // if
    if (siz > 0)
        memcpy(retval->literals, src->literals, siz);
    retval->literal_count = src->literal_count;

    retval->temp_count = src->temp_count;

    siz = sizeof (MOJOSHADER_symbol) * src->symbol_count;
    retval->symbols = (MOJOSHADER_symbol *) m(siz, d);
    if (retval->symbols == NULL)
    {
        MOJOSHADER_freePreshader(retval);
        return NULL;
    } // if
    memset(retval->symbols, '\0', siz);
    retval->symbol_count = src->symbol_count;

    for (i = 0; i < (int) retval->symbol_count; i++)
        if (!copysymbol(&retval->symbols[i], &src->symbols[i], m, d))
        {
            MOJOSHADER_freePreshader(retval);
            return NULL;
        } // if

    siz = sizeof (MOJOSHADER_preshaderInstruction) * src->instruction_count;
    retval->instructions = (MOJOSHADER_preshaderInstruction *) m(siz, d);
    if (retval->instructions == NULL)
    {
        MOJOSHADER_freePreshader(retval);
        return NULL;
    } // if
    if (siz > 0)
        memcpy(retval->instructions, src->instructions, siz);
    retval->instruction_count = src->instruction_count;
    // The memcpy above copied src's array_registers pointers into the clone; null them so a
    // later failure (or the free on one) never touches or double-frees src's arrays. -CNA
    for (i = 0; i < (int) retval->instruction_count; i++)
        for (j = 0; j < (int) retval->instructions[i].operand_count; j++)
            retval->instructions[i].operands[j].array_registers = NULL;
    for (i = 0; i < (int) retval->instruction_count; i++)
        for (j = 0; j < (int) retval->instructions[i].operand_count; j++)
        {
            siz = sizeof (unsigned int) * retval->instructions[i].operands[j].array_register_count;
            if (siz == 0)
                continue;
            retval->instructions[i].operands[j].array_registers = (unsigned int *) m(siz, d);
            if (retval->instructions[i].operands[j].array_registers == NULL)
            {
                MOJOSHADER_freePreshader(retval);
                return NULL;
            } // if
            memcpy(retval->instructions[i].operands[j].array_registers,
                   src->instructions[i].operands[j].array_registers,
                   siz);
        } // for

    siz = sizeof (float) * 4 * src->register_count;
    retval->registers = (float *) m(siz, d);
    if (retval->registers == NULL)
    {
        MOJOSHADER_freePreshader(retval);
        return NULL;
    } // if
    if (siz > 0)
        memcpy(retval->registers, src->registers, siz);
    retval->register_count = src->register_count;

    return retval;
} // copypreshader


MOJOSHADER_effect *MOJOSHADER_cloneEffect(const MOJOSHADER_effect *effect)
{
    int i, j, k;
    MOJOSHADER_parseData *pd;
    MOJOSHADER_effect *clone;
    MOJOSHADER_malloc m;
    void *d;
    uint32 siz = 0;
    char *stringcopy = NULL;
    uint32 curSampler;

    // The allocator is read only once the effect is known to be a real one: NULL and the static
    // error effects have none. -CNA
    if ((effect == NULL) || effect_is_static_error(effect))
        return NULL;  // no-op.
    m = effect->ctx.m;
    d = effect->ctx.malloc_data;

    clone = (MOJOSHADER_effect *) m(sizeof (MOJOSHADER_effect), d);
    if (clone == NULL)
        return NULL; // Maybe out_of_mem_effect instead?
    memset(clone, '\0', sizeof (MOJOSHADER_effect));

    /* Copy ctx */
    memcpy(&clone->ctx, &effect->ctx, sizeof(MOJOSHADER_effectShaderContext));

    /* Unlike the file's other COPY_STRING (see above), this one dereferenced without a NULL
     * check -- and readstring() returns NULL for a zero-length string, which is ordinary content,
     * not just hostile content. -CNA
     */
    #define COPY_STRING(location) \
        if (effect->location != NULL) \
        { \
        siz = strlen(effect->location) + 1; \
        stringcopy = (char *) m(siz, d); \
        if (stringcopy == NULL) \
            goto cloneEffect_outOfMemory; \
        strcpy(stringcopy, effect->location); \
        clone->location = stringcopy; \
        } \

    /* Copy errors */
    siz = sizeof (MOJOSHADER_error) * effect->error_count;
    clone->error_count = effect->error_count;
    clone->errors = (MOJOSHADER_error *) m(siz, d);
    if (clone->errors == NULL)
        goto cloneEffect_outOfMemory;
    memset(clone->errors, '\0', siz);
    for (i = 0; i < clone->error_count; i++)
    {
        COPY_STRING(errors[i].error)
        COPY_STRING(errors[i].filename)
        clone->errors[i].error_position = effect->errors[i].error_position;
    } // for

    /* Copy parameters */
    siz = sizeof (MOJOSHADER_effectParam) * effect->param_count;
    clone->param_count = effect->param_count;
    clone->params = (MOJOSHADER_effectParam *) m(siz, d);
    if (clone->params == NULL)
        goto cloneEffect_outOfMemory;
    memset(clone->params, '\0', siz);
    for (i = 0; i < clone->param_count; i++)
    {
        if (!copyvalue(&clone->params[i].value, &effect->params[i].value, m, d))
            goto cloneEffect_outOfMemory;

        /* Copy parameter annotations */
        siz = sizeof (MOJOSHADER_effectAnnotation) * effect->params[i].annotation_count;
        clone->params[i].annotation_count = effect->params[i].annotation_count;
        clone->params[i].annotations = (MOJOSHADER_effectAnnotation *) m(siz, d);
        if (clone->params[i].annotations == NULL)
            goto cloneEffect_outOfMemory;
        memset(clone->params[i].annotations, '\0', siz);
        for (j = 0; j < clone->params[i].annotation_count; j++)
            if (!copyvalue(&clone->params[i].annotations[j],
                           &effect->params[i].annotations[j],
                           m, d))
                goto cloneEffect_outOfMemory;
    } // for

    /* Copy techniques */
    siz = sizeof (MOJOSHADER_effectTechnique) * effect->technique_count;
    clone->technique_count = effect->technique_count;
    clone->techniques = (MOJOSHADER_effectTechnique *) m(siz, d);
    if (clone->techniques == NULL)
        goto cloneEffect_outOfMemory;
    memset(clone->techniques, '\0', siz);
    for (i = 0; i < clone->technique_count; i++)
    {
        COPY_STRING(techniques[i].name)

        /* Copy passes */
        siz = sizeof (MOJOSHADER_effectPass) * effect->techniques[i].pass_count;
        clone->techniques[i].pass_count = effect->techniques[i].pass_count;
        clone->techniques[i].passes = (MOJOSHADER_effectPass *) m(siz, d);
        if (clone->techniques[i].passes == NULL)
            goto cloneEffect_outOfMemory;
        memset(clone->techniques[i].passes, '\0', siz);
        for (j = 0; j < clone->techniques[i].pass_count; j++)
        {
            COPY_STRING(techniques[i].passes[j].name)

            /* Copy pass states */
            siz = sizeof (MOJOSHADER_effectState) * effect->techniques[i].passes[j].state_count;
            clone->techniques[i].passes[j].state_count = effect->techniques[i].passes[j].state_count;
            clone->techniques[i].passes[j].states = (MOJOSHADER_effectState *) m(siz, d);
            if (clone->techniques[i].passes[j].states == NULL)
                goto cloneEffect_outOfMemory;
            memset(clone->techniques[i].passes[j].states, '\0', siz);
            for (k = 0; k < clone->techniques[i].passes[j].state_count; k++)
            {
                const MOJOSHADER_effectState *srcstate =
                    &effect->techniques[i].passes[j].states[k];
                MOJOSHADER_effectState *dststate =
                    &clone->techniques[i].passes[j].states[k];
                dststate->type = srcstate->type;
                dststate->index = srcstate->index;
                dststate->referenced_parameter = srcstate->referenced_parameter;
                if (srcstate->preshader != NULL)
                {
                    dststate->preshader = copypreshader(
                        srcstate->preshader, m, effect->ctx.f, d);
                    if (dststate->preshader == NULL)
                        goto cloneEffect_outOfMemory;
                } // if
                dststate->preshader_param_count = srcstate->preshader_param_count;
                if (srcstate->preshader_param_count > 0)
                {
                    siz = sizeof (uint32) * srcstate->preshader_param_count;
                    dststate->preshader_params = (uint32 *) m(siz, d);
                    if (dststate->preshader_params == NULL)
                        goto cloneEffect_outOfMemory;
                    memcpy(dststate->preshader_params,
                           srcstate->preshader_params, siz);
                } // if
                if (!copyvalue(&dststate->value,
                               &srcstate->value,
                               m, d))
                    goto cloneEffect_outOfMemory;
            } // for

            /* Copy pass annotations */
            siz = sizeof (MOJOSHADER_effectAnnotation) * effect->techniques[i].passes[j].annotation_count;
            clone->techniques[i].passes[j].annotation_count = effect->techniques[i].passes[j].annotation_count;
            clone->techniques[i].passes[j].annotations = (MOJOSHADER_effectAnnotation *) m(siz, d);
            if (clone->techniques[i].passes[j].annotations == NULL)
                goto cloneEffect_outOfMemory;
            memset(clone->techniques[i].passes[j].annotations, '\0', siz);
            for (k = 0; k < clone->techniques[i].passes[j].annotation_count; k++)
                if (!copyvalue(&clone->techniques[i].passes[j].annotations[k],
                               &effect->techniques[i].passes[j].annotations[k],
                               m, d))
                    goto cloneEffect_outOfMemory;
        } // for

        /* Copy technique annotations */
        siz = sizeof (MOJOSHADER_effectAnnotation) * effect->techniques[i].annotation_count;
        clone->techniques[i].annotation_count = effect->techniques[i].annotation_count;
        clone->techniques[i].annotations = (MOJOSHADER_effectAnnotation *) m(siz, d);
        if (clone->techniques[i].annotations == NULL)
            goto cloneEffect_outOfMemory;
        memset(clone->techniques[i].annotations, '\0', siz);
        for (j = 0; j < clone->techniques[i].annotation_count; j++)
            if (!copyvalue(&clone->techniques[i].annotations[j],
                           &effect->techniques[i].annotations[j],
                           m, d))
                goto cloneEffect_outOfMemory;
    } // for

    /* Copy the current technique, but do NOT copy the pass, pass >= 0 just
     * means that the effect we're cloning is currently active
     */
    for (i = 0; i < effect->technique_count; i++)
        if (&effect->techniques[i] == effect->current_technique)
        {
            clone->current_technique = &clone->techniques[i];
            break;
        } // if
    assert(clone->current_technique != NULL);
    clone->current_pass = -1;

    /* Copy object table */
    siz = sizeof (MOJOSHADER_effectObject) * effect->object_count;
    clone->object_count = effect->object_count;
    clone->objects = (MOJOSHADER_effectObject *) m(siz, d);
    if (clone->objects == NULL)
        goto cloneEffect_outOfMemory;
    memset(clone->objects, '\0', siz);
    for (i = 0; i < clone->object_count; i++)
    {
        clone->objects[i].type = effect->objects[i].type;
        if (clone->objects[i].type == MOJOSHADER_SYMTYPE_PIXELSHADER
         || clone->objects[i].type == MOJOSHADER_SYMTYPE_VERTEXSHADER)
        {
            clone->objects[i].shader.technique = effect->objects[i].shader.technique;
            clone->objects[i].shader.pass = effect->objects[i].shader.pass;
            clone->objects[i].shader.is_preshader = effect->objects[i].shader.is_preshader;
            siz = sizeof (uint32) * effect->objects[i].shader.preshader_param_count;
            clone->objects[i].shader.preshader_param_count = effect->objects[i].shader.preshader_param_count;
            clone->objects[i].shader.preshader_params = (uint32 *) m(siz, d);
            if ((siz > 0) && (clone->objects[i].shader.preshader_params == NULL))
                goto cloneEffect_outOfMemory;
            if (siz > 0)
                memcpy(clone->objects[i].shader.preshader_params,
                       effect->objects[i].shader.preshader_params,
                       siz);
            siz = sizeof (uint32) * effect->objects[i].shader.param_count;
            clone->objects[i].shader.param_count = effect->objects[i].shader.param_count;
            clone->objects[i].shader.params = (uint32 *) m(siz, d);
            if ((siz > 0) && (clone->objects[i].shader.params == NULL))
                goto cloneEffect_outOfMemory;
            if (siz > 0)
                memcpy(clone->objects[i].shader.params,
                       effect->objects[i].shader.params,
                       siz);

            if (clone->objects[i].shader.is_preshader)
            {
                clone->objects[i].shader.preshader = copypreshader(
                    effect->objects[i].shader.preshader, m, effect->ctx.f, d);
                if (clone->objects[i].shader.preshader == NULL)
                    goto cloneEffect_outOfMemory;
                continue;
            } // if

            effect->ctx.shaderAddRef(effect->objects[i].shader.shader);
            clone->objects[i].shader.shader = effect->objects[i].shader.shader;
            pd = clone->ctx.getParseData(clone->objects[i].shader.shader);

            siz = sizeof (MOJOSHADER_samplerStateRegister) * effect->objects[i].shader.sampler_count;
            clone->objects[i].shader.sampler_count = effect->objects[i].shader.sampler_count;
            clone->objects[i].shader.samplers = (MOJOSHADER_samplerStateRegister *) m(siz, d);
            if (clone->objects[i].shader.samplers == NULL)
                goto cloneEffect_outOfMemory;
            curSampler = 0;
            // A shader object whose program failed to compile has no parse data, and the symbol
            // and parameter tables it indexes are both sized by the file being cloned. -CNA
            if ((pd == NULL) || (clone->objects[i].shader.params == NULL))
                continue;
            for (j = 0; j < pd->symbol_count; j++)
                if (pd->symbols[j].register_set == MOJOSHADER_SYMREGSET_SAMPLER)
                {
                    uint32 par;
                    if (curSampler >= clone->objects[i].shader.sampler_count) break;
                    if (j >= (int) clone->objects[i].shader.param_count) break;
                    par = clone->objects[i].shader.params[j];
                    if (par >= (uint32) clone->param_count) break;
                    clone->objects[i].shader.samplers[curSampler].sampler_name = clone->params[par].value.name;
                    clone->objects[i].shader.samplers[curSampler].sampler_register = pd->symbols[j].register_index;
                    clone->objects[i].shader.samplers[curSampler].sampler_state_count = clone->params[par].value.value_count;
                    clone->objects[i].shader.samplers[curSampler].sampler_states = clone->params[par].value.valuesSS;
                    curSampler++;
                } // if
        } // if
        else if (clone->objects[i].type == MOJOSHADER_SYMTYPE_SAMPLER
              || clone->objects[i].type == MOJOSHADER_SYMTYPE_SAMPLER1D
              || clone->objects[i].type == MOJOSHADER_SYMTYPE_SAMPLER2D
              || clone->objects[i].type == MOJOSHADER_SYMTYPE_SAMPLER3D
              || clone->objects[i].type == MOJOSHADER_SYMTYPE_SAMPLERCUBE)
        {
            COPY_STRING(objects[i].mapping.name)
        } // else if
        else if (clone->objects[i].type == MOJOSHADER_SYMTYPE_STRING)
        {
            COPY_STRING(objects[i].string.string)
        } // else if
    } // for

    #undef COPY_STRING

    return clone;

cloneEffect_outOfMemory:
    MOJOSHADER_deleteEffect(clone);
    return NULL;
} // MOJOSHADER_cloneEffect


void MOJOSHADER_effectSetRawValueHandle(const MOJOSHADER_effectParam *parameter,
                                        const void *data,
                                        const unsigned int offset,
                                        const unsigned int len)
{
    // !!! FIXME: char* case is arbitary, for Win32 -flibit
    memcpy((char *) parameter->value.values + offset, data, len);
} // MOJOSHADER_effectSetRawValueHandle


void MOJOSHADER_effectSetRawValueName(const MOJOSHADER_effect *effect,
                                      const char *name,
                                      const void *data,
                                      const unsigned int offset,
                                      const unsigned int len)
{
    int i;
    for (i = 0; i < effect->param_count; i++)
    {
        if (strcmp(name, effect->params[i].value.name) == 0)
        {
            // !!! FIXME: char* case is arbitary, for Win32 -flibit
            memcpy((char *) effect->params[i].value.values + offset, data, len);
            return;
        } // if
    } // for
    assert(0 && "Effect parameter not found!");
} // MOJOSHADER_effectSetRawValueName


const MOJOSHADER_effectTechnique *MOJOSHADER_effectGetCurrentTechnique(const MOJOSHADER_effect *effect)
{
    return effect->current_technique;
} // MOJOSHADER_effectGetCurrentTechnique


void MOJOSHADER_effectSetTechnique(MOJOSHADER_effect *effect,
                                   const MOJOSHADER_effectTechnique *technique)
{
    int i;
    for (i = 0; i < effect->technique_count; i++)
    {
        if (technique == &effect->techniques[i])
        {
            effect->current_technique = technique;
            return;
        } // if
    } // for
    assert(0 && "Technique is not part of this effect!");
} // MOJOSHADER_effectSetTechnique


const MOJOSHADER_effectTechnique *MOJOSHADER_effectFindNextValidTechnique(const MOJOSHADER_effect *effect,
                                                                          const MOJOSHADER_effectTechnique *technique
)
{
    int i;
    if (technique == NULL)
        return &effect->techniques[0];
    for (i = 0; i < effect->technique_count; i++)
    {
        if (technique == &effect->techniques[i])
        {
            if (i == effect->technique_count - 1)
                return NULL; /* We were passed the last technique! */
            return &effect->techniques[i + 1];
        } // if
    } // for
    assert(0 && "Technique is not part of this effect!");
    return NULL;
} // MOJOSHADER_effectFindNextValidTechnique


void MOJOSHADER_effectBegin(MOJOSHADER_effect *effect,
                            unsigned int *numPasses,
                            int saveShaderState,
                            MOJOSHADER_effectStateChanges *stateChanges)
{
    *numPasses = effect->current_technique->pass_count;
    effect->restore_shader_state = saveShaderState;
    effect->state_changes = stateChanges;

    if (effect->restore_shader_state)
    {
        effect->ctx.getBoundShaders(effect->ctx.shaderContext,
                                    &effect->prev_vertex_shader,
                                    &effect->prev_pixel_shader);
    } // if
} // MOJOSHADER_effectBegin


void MOJOSHADER_effectBeginPass(MOJOSHADER_effect *effect,
                                unsigned int pass)
{
    int i;
    MOJOSHADER_effectPass *curPass;
    MOJOSHADER_effectState *state;
    MOJOSHADER_effectShader *rawVert = effect->current_vert_raw;
    MOJOSHADER_effectShader *rawPixl = effect->current_pixl_raw;
    int has_preshader = 0;

    effect->ctx.getBoundShaders(effect->ctx.shaderContext,
                                &effect->current_vert,
                                &effect->current_pixl);

    assert(effect->current_pass == -1);
    effect->current_pass = pass;
    curPass = &effect->current_technique->passes[pass];

    // !!! FIXME: I bet this could be stored at parse/compile time. -flibit
    for (i = 0; i < curPass->state_count; i++)
    {
        state = &curPass->states[i];
        if ((state->type == MOJOSHADER_RS_VERTEXSHADER) ||
            (state->type == MOJOSHADER_RS_PIXELSHADER))
        {
            // The object index is parsed content, and MOJOSHADER_effectObject is a union: an
            // index that is out of range, or that names an object which is not a shader at all,
            // would make every field read below garbage rather than merely wrong. -CNA
            const int object_index =
                (state->value.values != NULL && state->value.value_count > 0)
                    ? *state->value.valuesI : -1;
            MOJOSHADER_effectObject *object;
            if ((object_index < 0) || (object_index >= effect->object_count))
                continue;
            object = &effect->objects[object_index];
            if ((object->type != MOJOSHADER_SYMTYPE_VERTEXSHADER) &&
                (object->type != MOJOSHADER_SYMTYPE_PIXELSHADER))
            {
                continue;
            } // if

            if (state->type == MOJOSHADER_RS_VERTEXSHADER)
            {
                rawVert = &object->shader;
                if (rawVert->is_preshader)
                    has_preshader = 1;
                else
                    effect->current_vert = rawVert->shader;
            } // if
            else
            {
                rawPixl = &object->shader;
                if (rawPixl->is_preshader)
                    has_preshader = 1;
                else
                    effect->current_pixl = rawPixl->shader;
            } // else
        } // if
    } // for

    effect->state_changes->render_state_changes = curPass->states;
    effect->state_changes->render_state_change_count = curPass->state_count;

    effect->current_vert_raw = rawVert;
    effect->current_pixl_raw = rawPixl;

    /* If this effect pass has an array of shaders, we get to wait until
     * CommitChanges to actually bind the final shaders.
     * -flibit
     */
    if (!has_preshader)
    {
        effect->ctx.bindShaders(effect->ctx.shaderContext,
                                effect->current_vert,
                                effect->current_pixl);
        if (effect->current_vert_raw != NULL)
        {
            effect->state_changes->vertex_sampler_state_changes = rawVert->samplers;
            effect->state_changes->vertex_sampler_state_change_count = rawVert->sampler_count;
        } // if
        if (effect->current_pixl_raw != NULL)
        {
            effect->state_changes->sampler_state_changes = rawPixl->samplers;
            effect->state_changes->sampler_state_change_count = rawPixl->sampler_count;
        } // if
    } // if

    MOJOSHADER_effectCommitChanges(effect);
} // MOJOSHADER_effectBeginPass


static inline void copy_parameter_data(MOJOSHADER_effectParam *params,
                                       unsigned int *param_loc,
                                       MOJOSHADER_symbol *symbols,
                                       unsigned int symbol_count,
                                       float *regf, int *regi, uint8 *regb,
                                       unsigned int regf_count,
                                       unsigned int regi_count,
                                       unsigned int regb_count)
{
    int i, j, r, c;

    i = 0;
    for (i = 0; i < symbol_count; i++)
    {
        const MOJOSHADER_symbol *sym = &symbols[i];
        const MOJOSHADER_effectValue *param = &params[param_loc[i]].value;

        // float/int registers are vec4, so they have 4 elements each
        const uint32 start = sym->register_index << 2;

        // A shader's constant table and the effect parameter it names can disagree in untrusted
        // content, so every copy is bounded by the value storage that was actually parsed rather
        // than by the register count the constant table claims.
        const uint32 wanted = sym->register_count << 2;
        uint32 cells = (wanted < param->value_count) ? wanted : param->value_count;

        // The destination is bounded too: a register index comes from the shader's own constant
        // table, which is parsed content, so it can address well past the file it writes into.
        // A zero count means the caller could not report a size. -CNA
        {
            // A preshader's register file is floats only, so its caller passes NULL for the int
            // and bool files; a symbol that claims one of those must be skipped, not written
            // through a null pointer. -CNA
            const int wants_float = (param->type.parameter_type == MOJOSHADER_SYMTYPE_FLOAT) ||
                                    (sym->register_set == MOJOSHADER_SYMREGSET_FLOAT4);
            const void *dst_ptr = wants_float ? (const void *) regf :
                (sym->register_set == MOJOSHADER_SYMREGSET_INT4) ? (const void *) regi :
                (sym->register_set == MOJOSHADER_SYMREGSET_BOOL) ? (const void *) regb : NULL;
            const uint32 dst_count = wants_float ? regf_count :
                (sym->register_set == MOJOSHADER_SYMREGSET_INT4) ? regi_count :
                (sym->register_set == MOJOSHADER_SYMREGSET_BOOL) ? regb_count : regf_count;
            if (dst_ptr == NULL) continue;
            const uint32 dst_start =
                (sym->register_set == MOJOSHADER_SYMREGSET_BOOL) ? (start >> 2) : start;
            if (dst_count != 0)
            {
                if (dst_start >= dst_count) continue;
                if ((dst_start + cells) > dst_count) cells = dst_count - dst_start;
            } // if
        }

        if (param->type.parameter_type == MOJOSHADER_SYMTYPE_FLOAT)
            memcpy(regf + start, param->valuesF, cells << 2);
        else if (sym->register_set == MOJOSHADER_SYMREGSET_FLOAT4)
        {
            // Structs are a whole different world...
            if (param->type.parameter_class == MOJOSHADER_SYMCLASS_STRUCT)
                memcpy(regf + start, param->valuesF, cells << 2);
            else
            {
                // Sometimes int/bool parameters get thrown into float registers...
                j = 0;
                while (j < sym->register_count)
                {
                    c = 0;
                    while (c < (int) param->type.columns)
                    {
                        if ((uint32) ((j << 2) + c) >= cells) break;
                        if ((uint32) ((j << 2) + c) >= param->value_count) break;
                        regf[start + (j << 2) + c] = (float) param->valuesI[(j << 2) + c];
                        c++;
                    } // while
                    if (c == 0) break;
                    j++;
                } // while
            } // else
        } // else if
        else if (sym->register_set == MOJOSHADER_SYMREGSET_INT4)
            memcpy(regi + start, param->valuesI, cells << 2);
        else if (sym->register_set == MOJOSHADER_SYMREGSET_BOOL)
        {
            j = 0;
            r = 0;
            while ((r < sym->register_count) && ((uint32) r < cells))
            {
                c = 0;
                while ((c < (int) param->type.columns) && ((r + c) < (int) sym->register_count) &&
                       ((uint32) (r + c) < cells))
                {
                    if ((uint32) ((j << 2) + c) >= param->value_count) break;
                    // regb is not a vec4, enjoy that 'start' bitshift! -flibit
                    regb[(start >> 2) + r + c] = param->valuesI[(j << 2) + c];
                    c++;
                } // while
                if (c == 0) break;
                r += c;
                j++;
            } // while
        } // else if
    } // for
} // copy_parameter_data


void MOJOSHADER_effectCommitChanges(MOJOSHADER_effect *effect)
{
    MOJOSHADER_effectShader *rawVert = effect->current_vert_raw;
    MOJOSHADER_effectShader *rawPixl = effect->current_pixl_raw;

    /* Used for shader selection from preshaders */
    int i, j;
    MOJOSHADER_effectValue *param;
    MOJOSHADER_parseData *pd;
    float selector;
    int shader_object;
    int selector_ran = 0;
    int break_preshader = 0;

    float *vs_reg_file_f, *ps_reg_file_f;
    int *vs_reg_file_i, *ps_reg_file_i;
    uint8 *vs_reg_file_b, *ps_reg_file_b;

    /* For effect passes with arrays of shaders, we have to run a preshader
     * that determines which shader to use, based on a parameter's value.
     * -flibit
     */
    // !!! FIXME: We're just running the preshaders every time. Blech. -flibit
    /* Every index and span below comes from parsed content, so each one is bounded against the
     * storage it addresses: a shader-array selector is chosen by a preshader the effect itself
     * supplied, and the parameter, register and object tables it reaches through are all sized by
     * the same untrusted file. -CNA
     */
    #define SELECT_SHADER_FROM_PRESHADER(raw, gls) \
        if (raw != NULL && raw->is_preshader && raw->preshader != NULL && \
            raw->preshader_param_count > 0 && raw->param_count > 0 && \
            raw->preshader_params != NULL && raw->params != NULL && \
            raw->preshader->symbols != NULL && raw->preshader->registers != NULL) \
        { \
            const unsigned int symcount = \
                (raw->preshader->symbol_count < raw->preshader_param_count) \
                    ? raw->preshader->symbol_count : raw->preshader_param_count; \
            for (i = 0; i < symcount; i++) \
            { \
                const unsigned int reg_index = raw->preshader->symbols[i].register_index; \
                unsigned int cells; \
                if (raw->preshader_params[i] >= (uint32) effect->param_count) continue; \
                param = &effect->params[raw->preshader_params[i]].value; \
                cells = param->value_count >> 2; \
                for (j = 0; j < cells; j++) \
                { \
                    const unsigned int columns = \
                        (param->type.columns < 4) ? param->type.columns : 4; \
                    if ((reg_index >= raw->preshader->register_count) || \
                        ((raw->preshader->register_count - reg_index) < (j + columns))) break; \
                    if (((j << 2) + columns) > param->value_count) break; \
                    memcpy(raw->preshader->registers + reg_index + j, \
                           param->valuesI + (j << 2), \
                           columns << 2); \
                } \
            } \
            run_preshader(raw->preshader, &selector, 1); \
            if (raw->params[0] >= (uint32) effect->param_count) break_preshader = 1; \
            else { \
            param = &effect->params[raw->params[0]].value; \
            /* The selector is whatever the preshader computed, so it is range-checked as a \
             * float before any conversion: casting an out-of-range float to an integer type is \
             * undefined, and on x86 it yields INT_MIN, which then indexes far below the array. \
             * The comparison also rejects NaN. -CNA */ \
            if (!(selector >= 0.0f) || !(selector < (float) param->value_count)) \
                break_preshader = 1; \
            else \
            { \
                shader_object = param->valuesI[(int) selector]; \
                if ((shader_object < 0) || (shader_object >= effect->object_count) || \
                    ((effect->objects[shader_object].type != MOJOSHADER_SYMTYPE_VERTEXSHADER) && \
                     (effect->objects[shader_object].type != MOJOSHADER_SYMTYPE_PIXELSHADER))) \
                    break_preshader = 1; \
                else \
                { \
                    raw = &effect->objects[shader_object].shader; \
                    gls = raw->shader; \
                    selector_ran = 1; \
                } \
            } \
            } \
        }
    SELECT_SHADER_FROM_PRESHADER(rawVert, effect->current_vert)
    SELECT_SHADER_FROM_PRESHADER(rawPixl, effect->current_pixl)
    #undef SELECT_SHADER_FROM_PRESHADER
    if (break_preshader)
    {
        // A selector that does not address a real shader object leaves the previously bound
        // shaders in place rather than binding whatever the index happened to land on. -CNA
        return;
    } // if
    if (selector_ran)
    {
        effect->ctx.bindShaders(effect->ctx.shaderContext,
                                effect->current_vert,
                                effect->current_pixl);
        if (effect->current_vert_raw != NULL)
        {
            effect->state_changes->vertex_sampler_state_changes = rawVert->samplers;
            effect->state_changes->vertex_sampler_state_change_count = rawVert->sampler_count;
        } // if
        if (effect->current_pixl_raw != NULL)
        {
            effect->state_changes->sampler_state_changes = rawPixl->samplers;
            effect->state_changes->sampler_state_change_count = rawPixl->sampler_count;
        } // if
    } // if

    /* This is where parameters are copied into the constant buffers.
     * If you're looking for where things slow down immensely, look at
     * the copy_parameter_data() and run_preshader() functions.
     * -flibit
     */
    // !!! FIXME: We're just copying everything every time. Blech. -flibit
    // !!! FIXME: We're just running the preshaders every time. Blech. -flibit
    // !!! FIXME: Will the preshader ever want int/bool registers? -flibit
    #define COPY_PARAMETER_DATA(raw, stage) \
        if (raw != NULL && raw->shader != NULL) \
        { \
            pd = effect->ctx.getParseData(raw->shader); \
            copy_parameter_data(effect->params, raw->params, \
                                pd->symbols, \
                                pd->symbol_count, \
                                stage##_reg_file_f, \
                                stage##_reg_file_i, \
                                stage##_reg_file_b, \
                                MOJOSHADER_EFFECT_MAX_REG_FILE_F * 4, \
                                MOJOSHADER_EFFECT_MAX_REG_FILE_I * 4, \
                                MOJOSHADER_EFFECT_MAX_REG_FILE_B); \
            if (pd->preshader) \
            { \
                copy_parameter_data(effect->params, raw->preshader_params, \
                                    pd->preshader->symbols, \
                                    pd->preshader->symbol_count, \
                                    pd->preshader->registers, \
                                    NULL, \
                                    NULL, \
                                    pd->preshader->register_count << 2, 0, 0); \
                run_preshader(pd->preshader, stage##_reg_file_f, \
                              MOJOSHADER_EFFECT_MAX_REG_FILE_F * 4); \
            } \
        }
    effect->ctx.mapUniformBufferMemory(effect->ctx.shaderContext,
                                       &vs_reg_file_f, &vs_reg_file_i, &vs_reg_file_b,
                                       &ps_reg_file_f, &ps_reg_file_i, &ps_reg_file_b);
    COPY_PARAMETER_DATA(rawVert, vs)
    COPY_PARAMETER_DATA(rawPixl, ps)
    if ((effect->current_technique != NULL) && (effect->current_pass >= 0) &&
        (effect->current_pass < effect->current_technique->pass_count))
    {
        MOJOSHADER_effectPass *pass =
            &effect->current_technique->passes[effect->current_pass];
        for (i = 0; i < pass->state_count; i++)
        {
            MOJOSHADER_effectState *state = &pass->states[i];
            unsigned int start;
            unsigned int cells;
            if ((state->type != MOJOSHADER_RS_PIXELSHADERCONSTANT) ||
                (state->value.valuesF == NULL) || (state->value.value_count == 0) ||
                (state->index >= MOJOSHADER_EFFECT_MAX_REG_FILE_F))
            {
                continue;
            } // if
            if ((state->preshader != NULL) &&
                ((state->preshader_param_count == 0) ||
                 (state->preshader_params != NULL)) &&
                (state->preshader_param_count == state->preshader->symbol_count) &&
                (state->preshader->register_count <= 0x3FFFFFFFu))
            {
                copy_parameter_data(effect->params, state->preshader_params,
                                    state->preshader->symbols,
                                    state->preshader->symbol_count,
                                    state->preshader->registers,
                                    NULL, NULL,
                                    state->preshader->register_count << 2,
                                    0, 0);
                run_preshader(state->preshader, state->value.valuesF,
                              state->value.value_count);
            } // if
            start = state->index << 2;
            cells = state->value.value_count;
            if (cells > ((MOJOSHADER_EFFECT_MAX_REG_FILE_F << 2) - start))
                cells = (MOJOSHADER_EFFECT_MAX_REG_FILE_F << 2) - start;
            memcpy(ps_reg_file_f + start, state->value.valuesF,
                   cells * sizeof (float));
        } // for
    } // if
    effect->ctx.unmapUniformBufferMemory(effect->ctx.shaderContext);
    #undef COPY_PARAMETER_DATA
} // MOJOSHADER_effectCommitChanges


void MOJOSHADER_effectEndPass(MOJOSHADER_effect *effect)
{
    assert(effect->current_pass != -1);
    effect->current_pass = -1;
} // MOJOSHADER_effectEndPass


void MOJOSHADER_effectEnd(MOJOSHADER_effect *effect)
{
    if (effect->restore_shader_state)
    {
        effect->restore_shader_state = 0;
        effect->ctx.bindShaders(effect->ctx.shaderContext,
                                effect->prev_vertex_shader,
                                effect->prev_pixel_shader);
    } // if

    effect->state_changes = NULL;
} // MOJOSHADER_effectEnd

#endif // MOJOSHADER_EFFECT_SUPPORT

// end of mojoshader_effects.c ...
