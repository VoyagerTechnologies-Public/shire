# SHIRE Atlas

**Your voyage starts in the SHIRE.**

SHIRE is the Software & Hardware Integration Runtime Environment.
One environment connects flight software, simulation, and ground systems so the whole team can run a Design Reference Mission.
Use the Atlas to build the lab, understand its architecture, configure a mission, develop components, and run operator exercises.

New to SHIRE?
Start with [Installation](manual/handbook/installation.md), then follow [Getting Started](manual/handbook/getting-started.md) to launch the default Design Reference Mission.

## Choose your path

| Goal | Start here |
| --- | --- |
| Prepare a development environment | [Installation](manual/handbook/installation.md) |
| Build and run the default lab | [Getting Started](manual/handbook/getting-started.md) |
| Understand how services exchange data | [System Architecture](manual/core-concepts/architecture.md) |
| Select a spacecraft or scenario | [Configuration](manual/how-to/configuration.md) |
| Develop or integrate a component | [Components](manual/how-to/components.md) |
| Create a component from the reference mold | [Component Mold](manual/how-to/component-mold.md) |
| Rehearse an operator workflow | [Scenarios](scenarios/overview.md) |
| Review mission intent and requirements | [Design Reference Mission](drm/concept-of-operations.md) |
| Find available verification evidence | [Verification and Validation](drm/verification-and-validation.md) |
| Diagnose a common problem | [FAQ](manual/handbook/faq.md) |
| Look up a term or command | [Glossary](reference/glossary.md) and [Quick Reference](reference/quick-reference.md) |

## Begin the journey

From the repository root, the default lab can be built and started with:

```sh
make
make start
```

The first build can take time because it prepares several container images and compiles the flight, ground, dynamics, and simulation software.
When startup completes, open YAMCS at [http://localhost:8090](http://localhost:8090) and the 42 VNC interface at [http://localhost:5801/vnc_auto.html](http://localhost:5801/vnc_auto.html).

Read [Getting Started](manual/handbook/getting-started.md) before commanding the lab or changing simulation time.

## What is in the Atlas

* **About** explains why SHIRE exists, who it serves, and where its current boundaries are.
* **Lab Manual** covers installation, architecture, flight and ground software, simulation, configuration, components, and development workflows.
* **Design Reference Mission** describes the example mission concept, proposed requirements, and verification approach.
* **Scenarios** provides operator exercises and clearly identifies which flows are implemented or still proposed.
* **Reference** collects terminology, commands, source locations, documentation versions, and contribution guidance.

## Join the fellowship

The repository source, configuration, and tests are authoritative for implemented behavior.
Documentation pages distinguish implemented behavior from mission intent and proposed requirements.

SHIRE is developed in public so a community of missions can learn from a shared reference environment.
Help improve the journey from day one to flight and back again.

Report reproducible defects through [GitHub Issues](https://github.com/VoyagerTechnologies-Public/shire/issues).
Share questions and ideas in [GitHub Discussions](https://github.com/VoyagerTechnologies-Public/shire/discussions).
