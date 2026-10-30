# Versions

The public Atlas describes the SHIRE repository rather than an independently versioned product.
Documentation and implementation should therefore be reviewed together at a specific Git revision.

## Public site

The documentation workflow validates pull requests and pushes to `main` or `dev`.
Only a successful push to `main` publishes the public GitHub Pages site.

The site represents the most recent successful push to `main`.
The repository has no release tags yet, and the `CHANGELOG.md` file records everything under an `Unreleased` heading.
The dependency images that the Makefile and CI use carry the placeholder tag `0.0.0`, which is not a release number.
The Atlas does not provide a separate hosted snapshot for any revision other than the latest `main` push.

## Local revision

Record the exact repository and submodule revisions when following a procedure or retaining verification evidence:

```sh
git rev-parse HEAD
git submodule status --recursive
git status --short
```

A nonempty `git status --short` result means the checkout includes changes beyond the recorded commit.
Include those changes or a patch when the result must be reproduced.

## Updating version sensitive content

Avoid phrases such as "latest" unless the source itself intentionally tracks a moving tag.
Prefer exact command names, configuration paths, and behavior visible in the current checkout.

When a change alters an interface or workflow:

1. Update the implementation and Atlas in the same pull request when practical
2. Update examples and expected results
3. Run the Atlas validation and strict build
4. Record the change under `Unreleased` in `CHANGELOG.md` when it affects users.

Release specific documentation snapshots become possible once the project creates release tags, and the current workflow does not generate them.

***
Last reviewed: 20261027
