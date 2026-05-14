/* Companion TU for v0.2.56-n-oso-dsymutil.c.  Compiled separately so
 * the link step has two distinct .o files in the OSO chain.  Two
 * functions in one .o is the case that exercised tcc's pre-v0.2.56
 * DW_AT_high_pc bug: the second function's range collapsed to empty
 * because tcc emitted high_pc as a size-offset under DW_FORM_data4
 * while Tiger's cctools dsymutil interprets it as an absolute
 * end-address.  The v0.2.56 PPC-narrow fix emits low_pc + size with
 * a text-section reloc so both functions get distinct ranges. */

int helper_add(int a, int b) {
    int sum = a + b;
    return sum;
}

int helper_mul(int a, int b) {
    int prod = a * b;
    return prod;
}
