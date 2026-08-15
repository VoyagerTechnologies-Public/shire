# SHIRE

The Software & Hardware Integration Runtime Environment (SHIRE) is an open source simulation environment that emulates satellite flight hardware and interfaces in software.
It lets teams develop, integrate, and test a complete mission from day one.

SHIRE is released under a permissive open source license.
SHIRE follows [Semantic Versioning](https://semver.org/).
See [CHANGELOG.md](CHANGELOG.md) for release history.

## Quick Start

Use a supported Linux environment or Windows 11 with WSL 2 and install these prerequisites:

* Docker Engine and Compose
* Make
* Git
* Python 3 with PyYAML (`python3 -c "import yaml"`)

Once they are installed, you can:

* Clone
  * `git clone --recurse-submodules https://github.com/VoyagerTechnologies-Public/shire.git`
  * `cd shire`
* Build
  * `make`
* Run
  * `make start`
* Use
  * Open GSW
    * `firefox localhost:8090`
  * Open 42
    * `firefox localhost:5801/vnc_auto.html`
  * Attach to containers to pause / play time
    * `docker attach shire-server-drm`
* Stop
  * CTRL+C
  * Inspect volumes and logs as desired
  * `make stop`

## Documentation

The SHIRE Atlas is maintained as a self contained project in [atlas](atlas) and configured by [atlas/zensical.toml](atlas/zensical.toml).
To preview it locally:

```sh
cd atlas
python3 -m pip install --requirement requirements.txt
python3 -m zensical serve
```
From `./atlas`, run `python3 -m zensical build --clean --strict` to verify the documentation before submitting a change.

## Project Status

See the repository activity, releases, and [CHANGELOG.md](CHANGELOG.md) for the current project status.
If the project is archived, its status will be shown on GitHub and noted here.

## Software Bill of Materials (SBOM)

A source level inventory of system components, declared third party dependencies, container images, build tooling, and known coverage gaps is in [SBOM.md](SBOM.md).
The machine readable SBOM is available as [sbom.cdx.json](sbom.cdx.json) in CycloneDX 1.6 JSON format.

## Disclaimer

THE SOFTWARE IS PROVIDED "AS IS," "WITH ALL FAULTS," AND WITHOUT WARRANTY OF ANY KIND.
TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THE COPYRIGHT HOLDERS, AUTHORS, MAINTAINERS, AND CONTRIBUTORS DISCLAIM ALL WARRANTIES, WHETHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE, NON-INFRINGEMENT, SECURITY, RELIABILITY, ACCURACY, AVAILABILITY, QUIET ENJOYMENT, AND ANY WARRANTIES ARISING OUT OF COURSE OF DEALING, COURSE OF PERFORMANCE, OR USAGE OF TRADE.
NO COPYRIGHT HOLDER, AUTHOR, MAINTAINER, OR CONTRIBUTOR WARRANTS THAT THE SOFTWARE WILL OPERATE WITHOUT INTERRUPTION, BE ERROR-FREE, BE SECURE, MEET YOUR REQUIREMENTS, OR BE FREE FROM DEFECTS, VULNERABILITIES, MALICIOUS CODE, DATA LOSS, OR OTHER HARMFUL COMPONENTS.

See [LICENSE](LICENSE) for the full terms.
