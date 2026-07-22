# Security policy

## Supported versions

Security fixes are applied to the latest release line and `main`.

## Reporting

Do not disclose a vulnerability in a public issue. Use GitHub's private security
advisory reporting for `getbible/getbiblesword`.

Include the affected version, module/configuration needed to reproduce the issue,
impact and a minimal reproducer. Do not include copyrighted or confidential module
content unless you are authorized to share it.

## Trust boundary

SWORD modules, configuration, filenames, keys, markup, media and cipher metadata
are untrusted input. `getbiblesword` does not execute module content, follow
artifact symlinks, fetch remote media, or accept cipher keys on the command line.
See [docs/threat-model.md](docs/threat-model.md) for the complete boundary.
