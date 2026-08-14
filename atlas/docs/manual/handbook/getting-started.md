# Getting Started with SHIRE

A short quick-start to get a up and running with SHIRE.

## Walkthrough

Note that the speed at which you can install is subject to your internet connection and the performance of your computer.
Gigabytes of data are required to be downloaded during this process.

* Ensure Docker, Docker Compose (v2+), Git, Make, and Python3 are installed (see [Installation](installation.md)).
* Clone the repository to your computer:
```bash
git clone --recurse-submodules https://github.com/VoyagerTechnologies-Public/shire.git
cd shire
```


* Prepare environment and build: `make`


* Start the lab: `make start`


* The various services will take a few seconds to stabilize.
* Flight software takes approximately 30 seconds to finish its initialization currently.


* Access GSW (YAMCS): http://localhost:8090


* You'll be able to send commands, view the current links, etc. in YAMCS so poke around!


* Attach to consoles in a new tab: `docker attach shire-server-sat-1`


* Stop (preserves data): CTRL+C in primary console


* If you did CTRL+C twice to stop quickly, you may need to `make stop` prior to running again.


* Clean (removes data): `make clean`


* Looking to reclaim some data? `make clean-cache`


* Want to uninstall? `make uninstall`


Have trouble with any of the above?
Checkout the [Frequently Asked Questions](faq.md).

----
Last updated: 20251202
