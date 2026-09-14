# SHIRE Third-Party Notices

SHIRE is a multi-license distribution. The root [Voyager license](LICENSE)
applies only to material authored by Voyager Technologies for SHIRE. It does
not supersede, replace, or relicense software, data, documentation, images, or
other material supplied in Git submodules or otherwise obtained from third
parties. Copyright and license conclusions are made at the file or component
boundary documented below; Voyager does not claim ownership of NASA or other
third-party material.

This inventory is release-hygiene documentation, not legal advice. An entry
marked **UNRESOLVED — legal/provenance review required** must be resolved or
expressly dispositioned by authorized counsel before a public release.

## Recursive submodule inventory

The commit is the Git link recorded by parent revision
`fc12054054249fb594cc315abcff51ef1b77813e`. Branch names in `.gitmodules` are
update hints and are not release pins.

<!-- BEGIN RECURSIVE SUBMODULE INVENTORY -->
| Path | Component | Checkout repository and evidenced upstream project | Pinned commit | Applicable license(s) | License or notice in checkout | Voyager-maintained fork | Modification and provenance status |
|---|---|---|---|---|---|---|---|
| `42` | 42 spacecraft simulator | [Voyager checkout](https://github.com/VoyagerTechnologies-Public/external-42); [evidenced upstream](https://github.com/ericstoneking/42) | `37defafe5e3fabfaa5d51b15d8220c7faa800d2f` | NASA Open Source Agreement; asset-specific credits and terms also apply | `42/License/NASA Open Source Software Agreement.pdf`, `42/License/Prominent Copyright Notice.txt`, `42/License/Credits.txt`, and file headers | Yes | Modified by Voyager on 2026-08-13 and 2026-09-14; see `provenance/NOSA_MODIFICATIONS.md`. Agreement version must be verified from the authoritative release record. |
| `cfs/apps/cf` | cFS CFDP application (CF) | [Voyager checkout](https://github.com/VoyagerTechnologies-Public/external-CF); [evidenced upstream](https://github.com/nasa/CF) | `deaeacfa0fe6ffd040a4d63e460941b591f5b169` | Apache-2.0 | `cfs/apps/cf/LICENSE` and file headers | Yes | Voyager fork contains SHIRE compatibility changes at the pin; Git history identifies the modifying commits. |
| `cfs/apps/ci_lab` | cFS Command Ingest Lab | [Voyager checkout](https://github.com/VoyagerTechnologies-Public/external-ci_lab); [evidenced upstream](https://github.com/nasa/ci_lab) | `68d91759f6f0cb50e8383dc358790598f8a277e8` | Apache-2.0 | `cfs/apps/ci_lab/LICENSE` and file headers | Yes | Voyager fork contains SHIRE scheduling changes at the pin; Git history identifies the modifying commits. |
| `cfs/apps/ds` | cFS Data Storage | [Voyager checkout](https://github.com/VoyagerTechnologies-Public/external-DS); [evidenced upstream](https://github.com/nasa/DS) | `f141f837199a7395c1c17fef3df6618f019cd08f` | Apache-2.0 | `cfs/apps/ds/LICENSE` and file headers | Yes | Voyager fork contains SHIRE setup changes at the pin; Git history identifies the modifying commits. |
| `cfs/apps/fm` | cFS File Manager | [Voyager checkout](https://github.com/VoyagerTechnologies-Public/external-FM); [evidenced upstream](https://github.com/nasa/FM) | `9b7a720b44f72dae9446dc558162e9f500be6a76` | Apache-2.0 | `cfs/apps/fm/LICENSE` and file headers | Yes | Voyager fork contains SHIRE compatibility changes at the pin; Git history identifies the modifying commits. |
| `cfs/apps/io_lib` | cFS Input/Output Library | [Voyager checkout](https://github.com/VoyagerTechnologies-Public/external-CFS_IO_LIB); [evidenced upstream](https://github.com/nasa/CFS_IO_LIB) | `c8a0771545a0828e6765a0b564a2412bcb9deb24` | NASA Open Source Agreement | License declaration in `cfs/apps/io_lib/README.md` and NASA notices in source headers; agreement text is absent | Yes | Modified by Voyager on 2026-08-13 and 2026-08-21; see `provenance/NOSA_MODIFICATIONS.md`. Restore the authoritative component agreement before release. |
| `cfs/apps/lc` | cFS Limit Checker | [Voyager checkout](https://github.com/VoyagerTechnologies-Public/external-LC); [evidenced upstream](https://github.com/nasa/LC) | `75f85088351f931958651a2b97d19ce5ffcb6567` | Apache-2.0 | `cfs/apps/lc/LICENSE` and file headers | Yes | Voyager fork contains SHIRE setup changes at the pin; Git history identifies the modifying commits. |
| `cfs/apps/sc` | cFS Stored Command | [Voyager checkout](https://github.com/VoyagerTechnologies-Public/external-SC); [evidenced upstream](https://github.com/nasa/SC) | `274d798553b915e94c7b02e516f40cf766deec10` | Apache-2.0 | `cfs/apps/sc/LICENSE` and file headers | Yes | Voyager fork contains SHIRE compatibility changes at the pin; Git history identifies the modifying commits. |
| `cfs/apps/sch` | cFS Scheduler | [Voyager checkout](https://github.com/VoyagerTechnologies-Public/external-SCH); [evidenced upstream](https://github.com/nasa/SCH) | `5aac56028b98fd3335b847bbd919a1371285f42c` | NASA Open Source Agreement | License declaration in `cfs/apps/sch/README.md` and NASA notices in source headers; agreement text is absent | Yes | Modified by Voyager on 2026-08-13, 2026-09-11, and 2026-09-14; see `provenance/NOSA_MODIFICATIONS.md`. Restore the authoritative component agreement before release. |
| `cfs/apps/to_lab` | cFS Telemetry Output Lab | [Voyager checkout](https://github.com/VoyagerTechnologies-Public/external-to_lab); [evidenced upstream](https://github.com/nasa/to_lab) | `7c3a9b788ca4d209bfc42647b8bdb87ff5edf9b5` | Apache-2.0 | `cfs/apps/to_lab/LICENSE` and file headers | Yes | Voyager fork contains SHIRE scheduling changes at the pin; Git history identifies the modifying commits. |
| `cfs/cfe` | Core Flight Executive (cFE) | [Voyager checkout](https://github.com/VoyagerTechnologies-Public/external-cFE); [evidenced upstream](https://github.com/nasa/cFE) | `3fcb62c31804ed11fb73670b86744e5735c90bc8` | Apache-2.0 | `cfs/cfe/LICENSE` and file headers | Yes | Voyager fork contains SHIRE observer and integration changes at the pin; Git history identifies the modifying commits. |
| `cfs/osal` | Operating System Abstraction Layer (OSAL) | [Voyager checkout](https://github.com/VoyagerTechnologies-Public/external-osal); [evidenced upstream](https://github.com/nasa/osal) | `3934fe868e08fc9e30e733a68ae674e1e47a5c2d` | Apache-2.0 | `cfs/osal/LICENSE` and file headers | Yes | Voyager fork contains SHIRE platform and timing changes at the pin; Git history identifies the modifying commits. |
| `cfs/psp` | cFE Platform Support Package (PSP) | [Voyager checkout](https://github.com/VoyagerTechnologies-Public/external-PSP); [evidenced upstream](https://github.com/nasa/PSP) | `6c71e0dd1aefe5392a100fecb29f13cf87d3e72c` | Apache-2.0 for PSP material; nested HWLib is separately licensed under NOSA 1.3 | `cfs/psp/LICENSE` and file headers | Yes | Voyager fork contains the SHIRE PSP and a separately pinned nested HWLib fork. Git history identifies the modifying commits. |
| `cfs/psp/fsw/hwlib` | Hardware Library (HWLib) | [Voyager checkout](https://github.com/VoyagerTechnologies-Public/external-hwlib); original upstream repository **UNRESOLVED — legal/provenance review required** | `c72e497b7ee714f350903a7d95ce523cb3c6985e` | NASA Open Source Agreement 1.3 (`NASA-1.3`), per maintainer determination | NASA copyright/warranty notices in source headers; the authoritative NOSA 1.3 text and NASA software designation are absent from the checkout | Yes | Modified by Voyager on 2026-08-13 and 2026-09-14; see `provenance/NOSA_MODIFICATIONS.md`. Original upstream repository/revision and pre-Voyager provenance remain unresolved. Nested `.gitmodules` also uses an SSH checkout URL. |
| `cfs/tools/elf2cfetbl` | cFS ELF-to-table tool | [Voyager checkout](https://github.com/VoyagerTechnologies-Public/external-elf2cfetbl); [evidenced upstream](https://github.com/nasa/elf2cfetbl) | `e888aa04fd4dcb77ace5ac218300266fe568f1dc` | Apache-2.0 | `cfs/tools/elf2cfetbl/LICENSE` and file headers | Yes, currently mirror-like | No Voyager-authored commit was identified at the pin in local history; exact equality with an authoritative upstream ref has not been independently verified. |
| `comp/cryptolib` | Core Flight System Cryptography Library (CryptoLib) | [Voyager checkout](https://github.com/VoyagerTechnologies-Public/external-CryptoLib); [evidenced upstream](https://github.com/nasa/CryptoLib) | `be524887ab9ad4efa8eed86e6988d7d81a129967` | NASA Open Source Agreement 1.3 (`NASA-1.3`) | `comp/cryptolib/LICENSE` and NASA notices in source files | Yes | Modified by Voyager on 2026-08-13, 2026-09-07, and 2026-09-14; see `provenance/NOSA_MODIFICATIONS.md`. |
| `yamcs` | SHIRE YAMCS integration, derived from Yamcs Quickstart | [Voyager checkout](https://github.com/VoyagerTechnologies-Public/external-yamcs); [evidenced upstream](https://github.com/yamcs/quickstart) | `bbd841c09b7b1e57c230c5b3f2e097e348e9edf9` | Declared `yamcs-core` and `yamcs-web` dependencies are AGPL-3.0 | No project license in the submodule; dependency license is declared by the [Yamcs project](https://github.com/yamcs/yamcs) | Yes | README identifies the checkout as a modified Quickstart fork. Neither the pinned fork nor evidenced Quickstart source includes a project-level license declaration. |
<!-- END RECURSIVE SUBMODULE INVENTORY -->

## Other third-party source in the root repository

| Path | Component | Upstream project | Revision | Applicable license(s) | License or notice | Modification/provenance status |
|---|---|---|---|---|---|---|
| `simulith/test/unity/` | Unity C test framework | [ThrowTheSwitch/Unity](https://github.com/ThrowTheSwitch/Unity) | Version 2.6.1 is encoded in `unity.h` | MIT | SPDX and copyright notices in all three source files; verbatim release license in `LICENSES/Unity-MIT.txt` | Vendored three-file snapshot. |

Package-manager dependencies, container base images, operating-system packages,
and build tools are inventoried in [SBOM.md](SBOM.md) and
[sbom.cdx.json](sbom.cdx.json). Their inclusion here does not imply that their
licenses apply to independent Voyager-authored SHIRE material.

## Distribution requirements

A source release must be produced from a recursive checkout. A plain
`git archive` of the root repository is insufficient because it records
submodule Git links but omits submodule contents. Source archives and binary or
container distributions must retain every applicable license, copyright,
attribution, change record, and notice, including 42 asset credits and NOSA
agreements.
