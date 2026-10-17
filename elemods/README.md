# The mods as .elemod files

Ready to add to [elekloader](https://github.com/irpina/elekloader): open it, choose your own official
Digitakt mk1 **OS 1.53** or **OS 1.54** file, **Install** these, tick the ones you want and build. Files
ending `-os1.54` are for OS 1.54, the others for 1.53: elekloader lists the ones that fit your file.
It brings the core mod (`core-4.0`); Digi Mono, Digi Poly and the `-chain` builds tick digichain with them,
and Digi Macro (MACRO) ticks Digi Mono.

| file | sha256 |
|---|---|
| `digichain-1.6-os1.54.elemod` | `5d4910f597874ac2` |
| `digichain-1.6.elemod` | `819f6045b5ad4dea` |
| `digieq-1.0c-os1.54.elemod` | `d0ee392ba21d6590` |
| `digieq-1.0c.elemod` | `eb5c75fbc5b1ecda` |
| `digifilter-1.1a-os1.54.elemod` | `7403e3270142ca58` |
| `digifilter-1.1a.elemod` | `86e302abe2213434` |
| `digihealth-1.0-os1.54.elemod` | `84c39e08eff429df` |
| `digihealth-1.0.elemod` | `b2ce4d6ad0bea162` |
| `digimacro-1.1-os1.54.elemod` | `cfb07c31450ce02c` |
| `digimacro-1.1.elemod` | `9cc981a5eb7f1ce0` |
| `digimatrix-1.0c-os1.54.elemod` | `ce04490838836956` |
| `digimatrix-1.0c.elemod` | `dc2c9d32dde13e75` |
| `digimono-0.23-os1.54.elemod` | `5750fcaf0699fd16` |
| `digimono-0.23.elemod` | `6b8b9b62bbc32216` |
| `digineighbor-0.7-os1.54.elemod` | `8a5425c56f89c3fa` |
| `digineighbor-0.7.elemod` | `cae48c35a37e1d8e` |
| `digipoly-2.0a-os1.54.elemod` | `d374422cd1a95b25` |
| `digipoly-2.0a.elemod` | `7ddeefb4616e27e6` |
| `digislicer-2.3-os1.54.elemod` | `c936cba7fcf61566` |
| `digislicer-2.3.elemod` | `1f4f11f39eaf9e1a` |
| `digisophie-1.1.13f-chain-os1.54.elemod` | `2a70b95c505613ee` |
| `digisophie-1.1.13f-chain.elemod` | `f33026cc6d5e1843` |
| `digiutils-1.9a-os1.54.elemod` | `ed13ca17753e4885` |
| `digiutils-1.9a.elemod` | `8697ac1fe883cb8f` |
| `tonefx-3.2a3-os1.54.elemod` | `c490c6f22fcdd727` |
| `tonefx-3.2a3.elemod` | `c37a8a727594392f` |

Built with elekloader 8972d70, 2026-10-15 (core `4.0`) by `tools/dev.sh publish`. Every pair combines with
core (`elekloader --check`). Several big mods together can need more than the mods' RAM (256 KB with core 4.0, 128 KB before):
elekloader says so when you build. An `.elemod` holds the mod's own code: where it repeats firmware
bytes, elekloader stores a reference to your own file instead, and the few original bytes at each place
it patches are there only to check your file. No firmware is stored here.

## The other authors' mods

These are built here from their authors' repositories at the commit named, their code unchanged but for what the last column says;
their licenses are in `licenses/`, and the source of each build is that commit plus what the last
column says (the tools named are in this repository). digi1_mods' own mods are under its license.

**Tone+FX** (`tonefx`) is a port kept in this repository (mods/tonefx): Digital Alchemy DSP's Tone+FX 3.2a
([DigiAlchemydsp/RingTone@0310695](https://github.com/DigiAlchemydsp/RingTone)), GPL-2.0-or-later
([text](licenses/tonefx-LICENSE)); its source is mods/tonefx, which marks what the port changed.

| mod | license | source | changed for this build |
|---|---|---|---|
| digisophie | MIT ([text](licenses/digisophie-LICENSE)) | [soejrd/digisophie@fee5f51](https://github.com/soejrd/digisophie/tree/fee5f514187fd3af999c866200f074c9a7045d50) | its render made lighter, the same output (tools/patches/digisophie-render.patch); the sites digichain owns moved to it (tools/chain_patch.py) |
| digislicer | GPL-2.0-or-later ([text](licenses/digislicer-LICENSE)) | [irpina/digislicer@d3515b2](https://github.com/irpina/digislicer/tree/d3515b20730ca374bf37e911a7ddf47265827f1e) | none (it uses elekloader's machine-pages; tools/chain_patch.py only moved older versions' sites to digichain) |
| digineighbor | GPL-2.0-or-later ([text](licenses/digineighbor-LICENSE)) | [irpina/digineighbor@f563922](https://github.com/irpina/digineighbor/tree/f5639220d4248882f60fd1f6d502ec25d8b6d221) | none (it uses elekloader's machine-pages; tools/chain_patch.py only moved older versions' sites to digichain) |
| digifilter | GPL-2.0-or-later ([text](licenses/digifilter-LICENSE)) | [DigiAlchemydsp/DigiFilter@404f72f](https://github.com/DigiAlchemydsp/DigiFilter/tree/404f72fa3a5dc62df06d8a5fa450f83dd72b70fe) | OS 1.54: ported by tools/port_os.py |
| digihealth | GPL-2.0-or-later ([text](licenses/digihealth-LICENSE)) | [irpina/digihealth@b2c3cf1](https://github.com/irpina/digihealth/tree/b2c3cf16eaaf0ed638214858a487c955b5efd5cc) | none |

