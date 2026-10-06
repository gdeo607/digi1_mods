# The mods as .elemod files

Ready to add to [elekloader](https://github.com/irpina/elekloader): open it, choose your own official
Digitakt mk1 **OS 1.53** or **OS 1.54** file, **Install** these, tick the ones you want and build. Files
ending `-os1.54` are for OS 1.54, the others for 1.53: elekloader lists the ones that fit your file.
It brings the core mod (`core-2.1`); Digi Mono, Digi Poly and the `-chain` builds tick digichain with them.

| file | sha256 |
|---|---|
| `digichain-1.6-os1.54.elemod` | `5d4910f597874ac2` |
| `digichain-1.6.elemod` | `819f6045b5ad4dea` |
| `digieq-1.0b-os1.54.elemod` | `82dcd78faf44e67b` |
| `digieq-1.0b.elemod` | `af5088bf444de4f1` |
| `digifilter-1.0i-os1.54.elemod` | `fd7ce4b0a20b93cc` |
| `digifilter-1.0i.elemod` | `acfb71cc7719d93d` |
| `digihealth-1.0-os1.54.elemod` | `b8a49c1137fb3f2a` |
| `digihealth-1.0.elemod` | `aba9413ee205e136` |
| `digimatrix-1.0b-os1.54.elemod` | `f12fd6c0f154f135` |
| `digimatrix-1.0b.elemod` | `792810c2432cadab` |
| `digimono-0.16-os1.54.elemod` | `77d1db6350400f26` |
| `digimono-0.16.elemod` | `db0efc3492906c4a` |
| `digineighbor-0.6-chain-os1.54.elemod` | `49fbcabbab7a8f7d` |
| `digineighbor-0.6-chain.elemod` | `4621ed88a23c86a6` |
| `digipoly-2.0-os1.54.elemod` | `4cc1462425315077` |
| `digipoly-2.0.elemod` | `1d2615c58e0a944c` |
| `digislicer-2.1-chain-os1.54.elemod` | `122c976f80179c9d` |
| `digislicer-2.1-chain.elemod` | `456b4adc4dd32e2e` |
| `digisophie-1.1.13-chain-os1.54.elemod` | `c035eec552d56628` |
| `digisophie-1.1.13-chain.elemod` | `b584c1e3884dc239` |
| `digiutils-1.9a-os1.54.elemod` | `ed13ca17753e4885` |
| `digiutils-1.9a.elemod` | `8697ac1fe883cb8f` |

Built with elekloader 793b2e4, 2026-10-04 (core `2.1`) by `tools/dev.sh publish`. Every pair combines with
core (`elekloader --check`). Several big mods together can need more than the 128 KB of mod RAM:
elekloader says so when you build. An `.elemod` holds the mod's own code: where it repeats firmware
bytes, elekloader stores a reference to your own file instead, and the few original bytes at each place
it patches are there only to check your file. No firmware is stored here.

## The other authors' mods

These are built here, unchanged in their code, from their authors' repositories at the commit named;
their licenses are in `licenses/`, and the source of each build is that commit plus what the last
column says (the tools named are in this repository). digi1_mods' own mods are under its license.

| mod | license | source | changed for this build |
|---|---|---|---|
| digisophie | MIT ([text](licenses/digisophie-LICENSE)) | [soejrd/digisophie@961c39c](https://github.com/soejrd/digisophie/tree/961c39cec699e8f8940391634aaba9fad7120792) | the sites digichain owns moved to it (tools/chain_patch.py); OS 1.54: ported by tools/port_os.py |
| digislicer | GPL-2.0-or-later ([text](licenses/digislicer-LICENSE)) | [irpina/digislicer@ac0c46d](https://github.com/irpina/digislicer/tree/ac0c46d8607205352adcd0f49fb09a4736edda17) | the sites digichain owns moved to it (tools/chain_patch.py) |
| digineighbor | GPL-2.0-or-later ([text](licenses/digineighbor-LICENSE)) | [irpina/digineighbor@d09574a](https://github.com/irpina/digineighbor/tree/d09574ab5fac8c079849e74c5c3efe129de54126) | the sites digichain owns moved to it (tools/chain_patch.py) |
| digifilter | GPL-2.0-or-later ([text](licenses/digifilter-LICENSE)) | [DigiAlchemydsp/DigiFilter@709f4af](https://github.com/DigiAlchemydsp/DigiFilter/tree/709f4af76d9a93fe58ae525aecf0447cd39dbdd1) | OS 1.54: ported by tools/port_os.py |
| digihealth | GPL-2.0-or-later ([text](licenses/digihealth-LICENSE)) | [irpina/digihealth@6d2a956](https://github.com/irpina/digihealth/tree/6d2a95605901f4f5ea6301dbad16e573380331a6) | none |

