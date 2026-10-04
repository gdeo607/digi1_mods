# The mods as .elemod files

Ready to add to [elekloader](https://github.com/irpina/elekloader): open it, choose your own official
Digitakt mk1 **OS 1.53** file, **Install** these, tick the ones you want and build. elekloader
brings the core mod (`core-2.1`); Digi Mono ticks digichain with it.

| file | sha256 |
|---|---|
| `digichain-1.4.elemod` | `e86d20a27aba6b99` |
| `digieq-1.0b.elemod` | `ff95f2f404c1ca8a` |
| `digimatrix-1.0b.elemod` | `e28dc7ca2c5840b1` |
| `digimono-0.11.elemod` | `d27c1390213e3742` |
| `digipoly-2.0.elemod` | `6858af67e54bb307` |
| `digiutils-1.9a.elemod` | `69139e34685fd1a2` |

Built with elekloader e4d8ba8, 2026-10-03 (core `2.1`) by `tools/dev.sh publish`. Every pair combines with
core (`elekloader --check`); so do they with the mods kept up to date by `tools/dev.sh mods`: digihealth,
DigiFilter and the `-chain` builds of SOPHIE, NEIGHBOR and DIGISLICER. An `.elemod` holds the mod's own
code: where it repeats firmware bytes, elekloader stores a reference to your own file instead, and the few
original bytes at each place it patches are there only to check your file. No firmware is stored here.

