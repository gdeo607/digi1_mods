# The mods as .elemod files

Ready to add to [elekloader](https://github.com/irpina/elekloader): open it, choose your own official
Digitakt mk1 **OS 1.53** or **OS 1.54** file, **Install** these, tick the ones you want and build. Files
ending `-os1.54` are for OS 1.54, the others for 1.53: elekloader lists the ones that fit your file.
It brings the core mod (`core-2.1`); Digi Mono and Digi Poly tick digichain with them.

| file | sha256 |
|---|---|
| `digichain-1.5-os1.54.elemod` | `43dc6d1c00f9970a` |
| `digichain-1.5.elemod` | `df3b8d8b670abcd1` |
| `digieq-1.0b-os1.54.elemod` | `82dcd78faf44e67b` |
| `digieq-1.0b.elemod` | `af5088bf444de4f1` |
| `digimatrix-1.0b-os1.54.elemod` | `f12fd6c0f154f135` |
| `digimatrix-1.0b.elemod` | `792810c2432cadab` |
| `digimono-0.13-os1.54.elemod` | `c8b8a35303fde65a` |
| `digimono-0.13.elemod` | `5a56e59d1dc15bd1` |
| `digipoly-2.0-os1.54.elemod` | `4cc1462425315077` |
| `digipoly-2.0.elemod` | `1d2615c58e0a944c` |
| `digiutils-1.9a-os1.54.elemod` | `ed13ca17753e4885` |
| `digiutils-1.9a.elemod` | `8697ac1fe883cb8f` |

Built with elekloader 6f378ce, 2026-10-04 (core `2.1`) by `tools/dev.sh publish`. Every pair combines with
core (`elekloader --check`); so do they with the mods kept up to date by `tools/dev.sh mods`: digihealth,
DigiFilter and the `-chain` builds of SOPHIE, NEIGHBOR and DIGISLICER. An `.elemod` holds the mod's own
code: where it repeats firmware bytes, elekloader stores a reference to your own file instead, and the few
original bytes at each place it patches are there only to check your file. No firmware is stored here.

