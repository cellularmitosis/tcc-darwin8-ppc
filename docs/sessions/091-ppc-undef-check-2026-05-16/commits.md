# Session 091 commits

- `9a2e672` **v0.2.67-g3: validate undefined externals against linked
  dylibs (roadmap #12)** — five coupled changes in `tcc/ppc-macho.c`:
  auto-load libSystem (+ sub-library recursion) into
  `s1->dynsymtab_section`; `macho_load_dll` refactored with an
  `as_subdylib` flag; symbol names stored with leading underscore
  intact; new `ppc_macho_check_symbols`; EXE-output gate in
  `macho_output_file`. Adds `demos/v0.2.67-undef-check.sh`. Bumps
  roadmap item #10 wording and marks item #12 done. Tag: `v0.2.67-g3`.
