# SHIRE
The Software & Hardware Integration Runtime Environment (SHIRE) is an open-source, software-only simulation environment that emulates satellite flight hardware and interfaces, so teams can develop, integrate, and test an end-to-end mission from day one.

SHIRE is released under a permissive open-source license.
SHIRE follows [Semantic Versioning](https://semver.org/).
See [CHANGELOG.md](CHANGELOG.md) for release history.

## Quick Start

Assuming you're running Linux, Mac, or Windows Subsystem for Linux you'll also need to install the following prerequisites:
* Docker Engine and Compose
* Make
* Git
* Python3

Once you have those installed you can:
* Clone
  * `git clone --recurse-submodules https://github.com/VoyagerTechnologies-Public/shire.git`
  * `cd shire`
* Build
  * `make`
* Run
  * `make start`
* Use
  * Attach to containers
    * `docker attach shire-server`
  * Open GSW
    * `firefox localhost:8090`
* Stop
  * CTRL+C
  * Inspect volumes and logs as desired
  * `make stop`

## Project Status

SHIRE is actively maintained by Voyager Technologies.
If the project is ever placed in maintenance-only mode, a notice will be posted in this README and all open issues.
The repository will not be deleted, it will be archived on GitHub (read-only) to preserve the reference mission for the community.

## Software Bill of Materials (SBOM)

A full inventory of system components, third-party dependencies, container images, and build tooling is in [SBOM.md](SBOM.md).
The machine-readable SBOM is available as [sbom.cdx.json](sbom.cdx.json) (CycloneDX 1.6 JSON).

## Disclaimer

THE SOFTWARE IS PROVIDED "AS IS," "WITH ALL FAULTS," AND WITHOUT WARRANTY OF ANY KIND.
TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THE COPYRIGHT HOLDERS, AUTHORS, MAINTAINERS, AND CONTRIBUTORS DISCLAIM ALL WARRANTIES, WHETHER EXPRESS, IMPLIED, STATUTORY, OR OTHERWISE, INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE, NON-INFRINGEMENT, SECURITY, RELIABILITY, ACCURACY, AVAILABILITY, QUIET ENJOYMENT, AND ANY WARRANTIES ARISING OUT OF COURSE OF DEALING, COURSE OF PERFORMANCE, OR USAGE OF TRADE.
NO COPYRIGHT HOLDER, AUTHOR, MAINTAINER, OR CONTRIBUTOR WARRANTS THAT THE SOFTWARE WILL OPERATE WITHOUT INTERRUPTION, BE ERROR-FREE, BE SECURE, MEET YOUR REQUIREMENTS, OR BE FREE FROM DEFECTS, VULNERABILITIES, MALICIOUS CODE, DATA LOSS, OR OTHER HARMFUL COMPONENTS.

See [LICENSE](LICENSE) for the full terms.
