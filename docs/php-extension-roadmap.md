# Native PHP extension roadmap

## Package identity

The planned Zend extension repository and PIE package are both:

```text
github.com/getbible/sword
getbible/sword
```

The PHP extension name is `getbiblesword`, Composer applications require
`ext-getbiblesword`, and user-facing classes use the `GetBible\Sword` namespace.

The extension links to the installed `libgetbiblesword.so.1` development
interface. Official PIE builds may compile the pinned getBibleSword and SWORD
sources as part of the extension package so the target server does not require a
separate `libsword`, `diatheke`, or `getbiblesword` program.

SWORD modules remain separately licensed data. They are not part of the engine or
PIE package.

## Release order

1. Complete and review the `feature/c-abi` branch.
2. Publish getBibleSword `0.3.0` with `libgetbiblesword.so.1`, headers and package
   metadata.
3. Create `getbible/sword` with the standard Zend `phpize` build.
4. Add PIE metadata and source-build verification for supported PHP versions.
5. Add the higher-level PHP API only after the low-level stream boundary is
   stable.

The extension repository must pin a compatible getBibleSword source release or
require ABI 1 through `pkg-config`/CMake. It must never include private C++ or
SWORD headers.

## First extension surface

The first extension should remain a small transport adapter:

```php
namespace GetBible\Sword;

final class Engine
{
    public static function productVersion(): string;
    public static function abiVersion(): int;
    public static function contract(): string;

    public function listToStream(string $modulePath, mixed $stream): void;

    public function extractToStream(
        string $modulePath,
        string $module,
        mixed $stream,
        int $artifactChunkSize = 1_048_576,
    ): void;
}
```

The implementation maps a PHP stream write to `gbs_write_callback`. It converts
every non-success status to a typed PHP exception and never returns a partially
validated extraction as an array or one large string.

A separate Composer library can validate the completed NDJSON, expose generators
and typed records, and provide application-specific conveniences. Composer's
`ext-getbiblesword` requirement verifies that the extension is loaded; Composer
does not itself compile the extension.

## Direct references

Methods such as:

```php
$engine->translations();
$engine->books('KJV');
$engine->chapter('KJV', 'John', 3);
$engine->verse('KJV', 'John', 3, 16);
$engine->passage('KJV', 'John', 3, 16, 21);
```

require additional C ABI operations. They must not be implemented by exposing
SWORD C++ objects to Zend or by parsing private module files in PHP.

The native ABI can add new versioned query functions under SONAME 1 when their
ownership and result contracts are additive. Each result should use a callback or
opaque handle with explicit lifetime rules, bounded memory, byte-preserving
values, PHP NTS/ZTS tests and representative coverage for Biblical texts,
commentaries, dictionaries and generic books.

## Module paths and installation

ABI v1 requires an explicit module path and performs no network access. A later
module manager may resolve paths in this order:

1. path passed by the application;
2. `getbiblesword.module_path` PHP INI setting;
3. `SWORD_PATH`;
4. a directory owned by the effective operating-system user.

PHP-FPM deployments should configure a stable writable path rather than relying
on a service user's home-directory discovery.

Automatic installation of a missing module needs its own native API and tests for:

- explicit opt-in and repository selection;
- CrossWire disclaimer and module-license handling;
- TLS and repository-metadata failures;
- process-safe locks across FPM workers;
- temporary download, validation and atomic rename;
- interrupted-install recovery;
- disk, file-count and download limits;
- read-only and offline deployments; and
- ambiguous, locked or unavailable modules.

Until those controls exist, the PHP extension must report a missing module
without attempting a download.

## PHP compatibility and CI

The extension repository should initially test supported maintained PHP versions
in both NTS and ZTS configurations where PIE supports them. Its acceptance gates
include:

- `phpize`, configure, build, PHPT and PIE package builds;
- AddressSanitizer and UndefinedBehaviorSanitizer native tests;
- callback cancellation and PHP stream write failures;
- repeated requests in one process;
- concurrent calls under ZTS, respecting ABI 1's internal SWORD serialization;
- CLI and FPM-compatible path/error behavior;
- bounded-memory extraction of a large module;
- no runtime dependency on `libsword.so` in official bundled packages;
- exact product, ABI and NDJSON contract reporting; and
- clean unload/shutdown with no leaked native state.

## Security boundary

In-process SWORD failures terminate the current PHP worker rather than an isolated
subprocess. The extension must therefore remain narrow, validate all PHP
arguments before entering native code, avoid global mutable request state and
document that process isolation remains the stronger boundary for untrusted
module repositories.
