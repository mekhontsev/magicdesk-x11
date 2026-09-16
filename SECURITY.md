# Security Reports

For a suspected vulnerability in MagicDesk's embedded X11 integration, use
[MagicDesk's private reporting channel](https://github.com/mekhontsev/magicdesk/security/advisories/new).
Include both the host build and this fork's exact revision, the affected
protocol and a minimal reproduction. Do not include live admission tokens,
Xauthority cookies or private file contents in a public issue.

The Android host owns admission, content grants and placement policy. The
embedded runtime owns its Binder authorization, X protocol, bounded transfers
and rendering. A local X client is not an independently sandboxed application;
do not expose the X server to untrusted clients or an unauthenticated network.

For an issue reproduced in unmodified upstream Termux:X11, follow
[Termux's security policy](https://termux.dev/security). Coordinate privately
when a shared issue affects both projects.
