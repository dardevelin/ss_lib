# Security Policy

## Supported versions

Security fixes are released for the latest 3.x version and, for programs not
yet migrated to 3.0, for the latest 2.2.x. Upgrade to the
[latest release](https://github.com/dardevelin/ss_lib/releases/latest) of your
major version to get them; releases within a major version keep the source and
binary interface. 2.2.x security fixes are made on the `release/2.x` branch.

| Version | Supported |
|---------|-----------|
| Latest 3.0.x | Yes |
| Latest 2.2.x | Security fixes only |
| Older 2.x | No: upgrade to the latest 2.2.x, or migrate to 3.0 ([docs/migration-3.0.md](docs/migration-3.0.md)) |
| Before 2.2.0 | No: affected by [GHSA-f5pm-2465-f895](https://github.com/dardevelin/ss_lib/security/advisories/GHSA-f5pm-2465-f895) |

If you copied `ss_lib_single.h` into your project, replace it with the one from
the latest release: a copy taken before 2.2.0 has the use-after-free described
in the advisory.

## Reporting a vulnerability

Report vulnerabilities privately through GitHub:
[Report a vulnerability](https://github.com/dardevelin/ss_lib/security/advisories/new)
(the Security tab, then "Report a vulnerability"). Please do not open a public
issue for a vulnerability before a fix is released.

Include:

- the SS_Lib version or commit, and how you got it (release archive, single
  header, Homebrew, built from source);
- the configuration macros you build with (`SS_USE_STATIC_MEMORY`,
  `SS_ENABLE_THREAD_SAFETY`, ...), the compiler and the platform;
- a small program that shows the problem, and what you expected instead.

## What happens next

The report stays private while it is investigated. A confirmed vulnerability
is fixed with a regression test that fails without the fix, released as a
patch version, and described in a GitHub security advisory that credits the
reporter unless they prefer otherwise.

## Published advisories

- [GHSA-f5pm-2465-f895](https://github.com/dardevelin/ss_lib/security/advisories/GHSA-f5pm-2465-f895):
  use-after-free when a slot unregisters its signal, calls `ss_cleanup()` (in
  3.0, `ss_bus_destroy()` on the emitting bus), or queues a deferred emission
  during a flush. Affects versions before 2.2.0; fixed in 2.2.0.
  `tests/test_reentrancy.c` and `make test-single` guard against it.
