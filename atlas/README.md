# SHIRE Atlas

The Software & Hardware Integration Runtime Environment (SHIRE) Atlas is a comprehensive documentation set designed to support users, developers, and contributors.

## Purpose

The Atlas serves as a single source of truth for all things related to SHIRE, ensuring clarity and consistency across the project.

## Licensing

The SHIRE Atlas is licensed under the repository's [Permissive Open-Source License](LICENSE).
By contributing, you agree that your contributions will be licensed under those terms unless explicitly noted otherwise.

## Development

The Atlas is built with [Zensical](https://zensical.org/).
From the `atlas/` directory, install the pinned dependency and start the live preview:

```sh
python3 -m pip install --requirement requirements.txt
python3 -m zensical serve
```

Run `python3 -m zensical build --clean --strict` to validate a production build.
