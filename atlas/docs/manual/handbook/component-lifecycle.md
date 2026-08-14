# Component Lifecycle

SHIRE uses a three-stage lifecycle for hardware component implementations. Understanding this model helps you know where to put new code, how to graduate a component for community reuse, and what the long-term maintenance path looks like.

---

## Stage 1 — In-Monorepo Development

New components are developed inside the main SHIRE repository under `comp/`. This keeps the component tightly coupled to the current FSW and simulator interfaces during active development, and means a single pull request can change the component, its cFS app, its XTCE definition, and its simulator all at once.

**How to start:**

```bash
python3 cfg/shire-comp-mold.py --name my-comp
```

This scaffolds `comp/my-comp/` with the standard layout:

```
comp/my-comp/
  cli/        # Checkout command-line tool (built natively)
  fsw/        # cFS flight software application
  gsw/        # XTCE telemetry/command definitions + YAMCS displays
  sim/        # Simulation library loaded by Simulith
  test-fsw/   # FSW unit tests (lcov coverage)
  test-sim/   # Simulator unit tests
```

Register the component in `cfg/shire-config.yaml` to include it in builds.

**When to stay in Stage 1:** During initial prototyping, interface churn, or any time the component needs atomic changes alongside the core FSW or simulator.

---

## Stage 2 — Submodule Extraction

Once a component reaches interface stability — its XTCE definitions, cFS app message IDs, and simulator API are unlikely to change structurally — it can be extracted to its own repository and wired back into SHIRE as a git submodule.

**Why extract?**

- Other missions can adopt the component without cloning the full SHIRE stack.
- The component gets its own issue tracker, release cycle, and changelog.
- Teams working only on the component don't need to check out FSW or simulator sources.

**How to extract:**

1. Create a new public repository, e.g., `VoyagerTechnologies-Public/shire-comp-adcs`.
2. Push the `comp/adcs/` directory as the initial commit of the new repo.
3. In the SHIRE monorepo, remove the directory and add it back as a submodule:

```bash
git rm -r comp/adcs
git submodule add https://github.com/VoyagerTechnologies-Public/shire-comp-adcs comp/adcs
git commit -m "extract adcs to submodule"
```

4. The `comp/adcs/` path in the SHIRE repo now points to a commit in `shire-comp-adcs`. Builds and CI are unchanged — submodules are transparent to Makefiles.

**Submodule update workflow:** When the component repo publishes a new release, bump the pointer in SHIRE:

```bash
git submodule update --remote comp/adcs
git add comp/adcs
git commit -m "bump shire-comp-adcs to vX.Y.Z"
```

---

## Stage 3 — Community Adoption

With the component as a standalone repository, external teams can fork it and adapt it to their own hardware. The `comp/demo/` template in the SHIRE monorepo always reflects the current expected Stage 1 structure, so new contributors can scaffold against it.

**Naming convention for extracted components:** `VoyagerTechnologies-Public/shire-comp-<name>` (e.g., `shire-comp-adcs`, `shire-comp-eps`).

**What stays in the monorepo:** The `comp/demo/` template, `comp/cryptolib/` (external dependency, not Voyager-authored), and any component still in Stage 1.

---

## Summary

| Stage | Location | When to use |
|-------|----------|-------------|
| 1 — Active development | `comp/<name>/` in `VoyagerTechnologies-Public/shire` | New components, unstable interfaces |
| 2 — Stable submodule | `VoyagerTechnologies-Public/shire-comp-<name>`, referenced as a submodule | Stable interface, ready for reuse |
| 3 — Community fork | Forked from Stage 2 repo | External mission adopting the component |

----
Last updated: 20260728
