# Session 037 findings — durable lessons

Three PPC encoding / ABI things worth remembering. The first two
cost me hours of debugging in this session.

## `@hi` vs `@ha` is paired with `ori` vs `addi`, NOT interchangeable

This is the single most important PPC-codegen gotcha I've hit.

**Rule**:
* `lis rD, %ha(addr)` paired with `addi rD, rD, %lo(addr)` — `addi`
  is sign-extending. `@ha = (addr + 0x8000) >> 16` compensates for
  the sign extension.
* `lis rD, %hi(addr)` paired with `ori rD, rD, %lo(addr)` — `ori`
  is zero-extending. Use the *plain* high half: `@hi = addr >> 16`.

**Mixing them silently corrupts the address by ±0x10000** whenever
`%lo`'s top bit is set, because the `+0x8000` adjustment in `@ha`
expects to be cancelled by the sign-extension carry that `ori`
never performs.

For Tiger libSystem, about half of the entry points have `%lo`'s
top bit set (memset, free, fprintf, malloc, abort, ...), so the
bug is hit constantly once any libc function is called. The
symptom on PPC is typically a transfer of control to a
related-but-wrong address, often inside the same library —
e.g. memset @ 0x90129660 mis-resolved to 0x90139660 lands inside
the malloc family on Tiger and produces "Deallocation of a pointer
not malloced" warnings before the actual SIGBUS.

`ppc_li32_r0` in ppc-gen.c gets this right (uses `lis + ori` with
plain `@hi`); `ppc_link.c::relocate_plt` gets this right after
`a140db0`. Anywhere else we materialize a 32-bit constant via two
instructions, double-check.

## `addi rD, r0, imm` is `rD = imm`, NOT `rD = r0 + imm`

A PPC encoding special case: when the rA field of `addi` is r0,
the assembler interprets r0 as the *literal value 0* rather than
"the contents of register r0". So `addi r1, r0, -128` is encoded
identically to `li r1, -128` — the value in r0 is ignored.

This bit me writing `gen_vla_sp_restore`:

```c
/* WRONG: this loads -128 into r1, not r0-128 */
o(0x80000000 | (0 << 21) | (PPC_FP_REG << 16) | (addr & 0xffff));
/* lwz r0, addr(r31) */
o(0x38000000 | (1 << 21) | (0 << 16) | ((-128) & 0xffff));
/* addi r1, r0, -128 -- but r0 is ignored! */
```

**Fix**: use any other scratch register. r12 is the conventional
PPC scratch in the Apple ABI:

```c
o(0x80000000 | (12 << 21) | (PPC_FP_REG << 16) | (addr & 0xffff));
/* lwz r12, addr(r31) */
o(0x38000000 | (1 << 21) | (12 << 16) | ((-128) & 0xffff));
/* addi r1, r12, -128 */
```

The same special case applies to several other instructions
(`addis`, `subf`, others where rA=0 means literal 0). Worth
double-checking when writing PPC codegen by hand.

## PPC PLT for tcc -run: skip the GOT, bake the address

The i386 / ARM PLT pattern is "jump-through-GOT" — each PLT entry
does an indirect call through a GOT slot, which dyld lazy-binds
on first call. That requires the GOT to be filled (with addresses
or with PLT0-relative offsets for lazy binding), and tccelf.c's
`fill_got` machinery does this correctly for those targets.

For PPC `-run` we don't need lazy binding — `relocate_syms` has
already eagerly resolved all undefined symbols via `dlsym` before
we get to `relocate_plt`. So we bake the absolute address directly
into the PLT entry as a 4-instruction stub:

```
lis   r12, %hi(target)
ori   r12, r12, %lo(target)
mtctr r12
bctr
```

The GOT in `-run` mode is essentially unused; we read the GOT's
relocation list (`s1->got->reloc`) only to map each PLT entry to
its symbol (via `r_offset == got_offset` lookup). The GOT slots'
*data* doesn't matter. This dodges the byte-order question
(tccelf.c writes the GOT in `write32le`; PPC reads it BE — but
since the GOT data isn't actually used, the mismatch is moot).

When dynsym becomes relevant (i.e. if/when we ever support
shared-library output on PPC), this approach won't scale and a
proper GOT-indirection PLT will be needed.

## Bonus: dlsym leading-underscore exceptions on Mach-O

`tccelf.c::relocate_syms` strips the leading underscore before
dlsym (Mach-O exports are underscore-prefixed in the symbol table
but dlsym takes the unprefixed form). A handful of tcc-internal
symbols are stored without the leading underscore in our symtab —
`dyld_stub_binding_helper` is the one we hit. The unconditional
strip mangles those to `yld_stub_binding_helper`.

**Pattern**: on dlsym failure with the stripped name, retry with
the full name as a fallback. See the patch in
`tccelf.c::relocate_syms` (session 037, `4d92814`).
