# The mods as .elemod files

Ready to add to [elekloader](https://github.com/irpina/elekloader): open it, choose your own official
Digitakt mk1 **OS 1.53** or **OS 1.54** file, **Install** these, tick the ones you want and build. Files
ending `-os1.54` are for OS 1.54, the others for 1.53: elekloader lists the ones that fit your file.
It brings the core mod (`core-4.0`); the SRC machine mods (Digi Mono, Digi Macro, DH-101, Digi Poly, SOPHIE,
DIGISLICER, NEIGHBOR) tick machine-pages, which draws their pages, and Digi Macro (MACRO) and DH-101 tick Digi Mono.

| file | sha256 |
|---|---|
| `digi101-0.25-os1.54.elemod` | `4285b1c619d6b8bc` |
| `digi101-0.25.elemod` | `ea7c3956baeeb2de` |
| `digieq-1.0c-os1.54.elemod` | `d0ee392ba21d6590` |
| `digieq-1.0c.elemod` | `eb5c75fbc5b1ecda` |
| `digifilter-1.1a-os1.54.elemod` | `7403e3270142ca58` |
| `digifilter-1.1a.elemod` | `86e302abe2213434` |
| `digihealth-1.0-os1.54.elemod` | `84c39e08eff429df` |
| `digihealth-1.0.elemod` | `b2ce4d6ad0bea162` |
| `digimacro-1.2-os1.54.elemod` | `d99823b2cd3c2933` |
| `digimacro-1.2.elemod` | `76803565917a2660` |
| `digimatrix-1.0c-os1.54.elemod` | `ce04490838836956` |
| `digimatrix-1.0c.elemod` | `dc2c9d32dde13e75` |
| `digimono-0.24-os1.54.elemod` | `00b060b864c4412d` |
| `digimono-0.24.elemod` | `8e4dd3ae443f85d6` |
| `digineighbor-0.7-os1.54.elemod` | `8a5425c56f89c3fa` |
| `digineighbor-0.7.elemod` | `cae48c35a37e1d8e` |
| `digipoly-2.1-os1.54.elemod` | `460dc14ed4cf531f` |
| `digipoly-2.1.elemod` | `6fc08067b4e38416` |
| `digislicer-2.3-os1.54.elemod` | `c936cba7fcf61566` |
| `digislicer-2.3.elemod` | `1f4f11f39eaf9e1a` |
| `digisophie-1.2.0-os1.54.elemod` | `4afc45020f8f401b` |
| `digisophie-1.2.0.elemod` | `8b93281ff902404c` |
| `digiutils-1.9a-os1.54.elemod` | `ed13ca17753e4885` |
| `digiutils-1.9a.elemod` | `8697ac1fe883cb8f` |
| `machine-pages-1.2-os1.54.elemod` | `e46f152200085fd1` |
| `machine-pages-1.2.elemod` | `36459ae4c12059ea` |
| `tonefx-3.2a3-os1.54.elemod` | `c490c6f22fcdd727` |
| `tonefx-3.2a3.elemod` | `c37a8a727594392f` |

Built with elekloader 8972d70, 2026-10-15 (core `4.0`), machine-pages 1.2 and DH-101 0.25 with 05d571c, by `tools/dev.sh publish`. Every pair combines with
core (`elekloader --check`). Several big mods together can need more than the mods' RAM (256 KB with core 4.0, 128 KB before):
elekloader says so when you build. An `.elemod` holds the mod's own code: where it repeats firmware
bytes, elekloader stores a reference to your own file instead, and the few original bytes at each place
it patches are there only to check your file. No firmware is stored here.

## The other authors' mods

These are built here from their authors' repositories at the commit named, their code unchanged but for what the last column says;
their licenses are in `licenses/`, and the source of each build is that commit plus what the last
column says (the tools named are in this repository). digi1_mods' own mods are under its license.

**machine-pages** (`machine-pages`) is elekloader's ([irpina/elekloader@05d571c](https://github.com/irpina/elekloader/tree/05d571c6d7968fb274e5fbc5c415fabb27c703f3/mods/machine-pages),
GPL-2.0-or-later, [text](licenses/machine-pages-LICENSE)), built unchanged: it draws the SRC machines' pages.

**DH-101** (`digi101`) is Robbie's, contributed to this repository (mods/digi101, MIT).

**Tone+FX** (`tonefx`) is a port kept in this repository (mods/tonefx): Digital Alchemy DSP's Tone+FX 3.2a
([DigiAlchemydsp/RingTone@0310695](https://github.com/DigiAlchemydsp/RingTone)), GPL-2.0-or-later
([text](licenses/tonefx-LICENSE)); its source is mods/tonefx, which marks what the port changed.

| mod | license | source | changed for this build |
|---|---|---|---|
| digisophie | MIT ([text](licenses/digisophie-LICENSE)) | [soejrd/digisophie@fee5f51](https://github.com/soejrd/digisophie/tree/fee5f514187fd3af999c866200f074c9a7045d50) | 1.2.0 as offered to its author: its pages drawn by machine-pages, its render lighter, the same output (tools/patches/digisophie-machine-pages.patch) |
| digislicer | GPL-2.0-or-later ([text](licenses/digislicer-LICENSE)) | [irpina/digislicer@d3515b2](https://github.com/irpina/digislicer/tree/d3515b20730ca374bf37e911a7ddf47265827f1e) | none |
| digineighbor | GPL-2.0-or-later ([text](licenses/digineighbor-LICENSE)) | [irpina/digineighbor@f563922](https://github.com/irpina/digineighbor/tree/f5639220d4248882f60fd1f6d502ec25d8b6d221) | none |
| digifilter | GPL-2.0-or-later ([text](licenses/digifilter-LICENSE)) | [DigiAlchemydsp/DigiFilter@404f72f](https://github.com/DigiAlchemydsp/DigiFilter/tree/404f72fa3a5dc62df06d8a5fa450f83dd72b70fe) | OS 1.54: ported by tools/port_os.py |
| digihealth | GPL-2.0-or-later ([text](licenses/digihealth-LICENSE)) | [irpina/digihealth@b2c3cf1](https://github.com/irpina/digihealth/tree/b2c3cf16eaaf0ed638214858a487c955b5efd5cc) | none |

