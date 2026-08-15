# Component Lifecycle

SHIRE currently develops its first party hardware models in this repository under `comp/`.
The extraction stages below are a recommended lifecycle for reusable components, not a statement that the current ADCS, Demo, EPS, or Radio components have already moved to separate repositories.

## Stage 1: Development in this repository

This is the implemented model today.
Keeping a component in the SHIRE repository allows one change to update its simulator, cFS app, ground definitions, command line client, and tests together.

Start from the component mold:

```bash
python3 cfg/shire-comp-mold.py my-comp
```

The scaffold follows this layout:

```text
comp/my-comp/
  cli/        # Native command line client
  gsw/        # XTCE definitions, displays, and procedures
  sim/        # Simulith component model
  test-fsw/   # cFS application tests
  test-sim/   # Simulator tests
  src/        # cFS application implementation
  shared/     # Shared wire protocol and device interface
```

The mold deliberately does not register the new component.
Complete the integration steps in [Component Mold](../how-to/component-mold.md), including configuration, message IDs, ground definitions, and tests.

Keep a component in this stage while its interfaces change frequently or while changes need to remain atomic with the rest of SHIRE.

## Stage 2: Optional repository extraction

Once interfaces and ownership are stable, maintainers may choose to extract a component into its own repository and reference it as a Git submodule.
That decision should define:

* a public repository and release policy
* maintainers and an issue tracker
* compatibility expectations for SHIRE revisions
* continuous integration that tests both the standalone component and its SHIRE integration
* a documented submodule update process

Extraction is not transparent to contributors: clones must initialize the new submodule, repository pointers must be updated deliberately, and changes spanning repositories require coordinated reviews.
Validate the build and public clone workflow before adopting this model.

## Stage 3: External adoption

A separately released component can be forked and adapted for another mission.
This remains an ecosystem goal rather than a guarantee that the component is portable to arbitrary hardware.
Consumers still need to validate wire protocols, message IDs, timing, device drivers, cFS configuration, and ground definitions for their target.

## Status summary

| Stage | Location | Repository status |
| --- | --- | --- |
| Development in this repository | `comp/<name>/` | Implemented for ADCS, Demo, EPS, and Radio |
| Repository extraction | Separate repository referenced as a submodule | Optional future maintenance choice |
| External adoption | Consumer fork or pinned release | Ecosystem goal with target validation required |

***
Last reviewed: 14 August 2026
