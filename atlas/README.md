# SHIRE Atlas

The Software & Hardware Integration Runtime Environment (SHIRE) Atlas is a comprehensive documentation set designed to support users, developers, and contributors.

## Purpose

The Atlas is the public guide to SHIRE usage, architecture, configuration, and the Design Reference Mission.
Implemented behavior claims should remain traceable to the repository source, configuration, tests, or generated artifacts.

## Licensing

The SHIRE Atlas is licensed under the repository's [Permissive Open Source License](LICENSE).
By contributing, you agree that your contributions will be licensed under those terms unless explicitly noted otherwise.

## Development

The Atlas is built with [Zensical](https://zensical.org/).
From the `atlas/` directory, install the pinned dependency and start the live preview:

```sh
python3 -m pip install --requirement requirements.txt
python3 -m zensical serve
```

From the repository root, run `make docs-check` to validate Atlas style, links, assets, commands, procedures, selected repository facts, and the production build.
