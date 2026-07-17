# ADR 0001: Preserve both official and source views

- Status: accepted
- Date: 2026-07-17

## Decision

Every export contains two views: the official logical interpretation produced by
CrossWire SWORD and an exact byte envelope of the configuration and module files.

## Rationale

SWORD's logical API is authoritative for keys, scopes, filters and entry attributes,
but no interpretation can prove that it understands future or module-specific data.
The source envelope makes omissions detectable and recovery possible. Keeping only
files would force downstream consumers to reimplement SWORD; keeping only rendered
entries would be lossy.

## Consequences

Exports are larger and may contain copyrighted source files. Consumers may select
the logical records they need, while archival and diagnostic tooling retains the
ability to verify every source byte.
