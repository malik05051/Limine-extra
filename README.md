# Limine [![Matrix Server](https://img.shields.io/matrix/limine:matrix.org?color=000000&label=Matrix&logo=matrix)](https://matrix.to/#/#limine:matrix.org)

<p align="center">
    <img src="https://github.com/Limine-Bootloader/Limine/blob/trunk/logo.png?raw=true" alt="Limine's logo"/>
</p>

# IMPORTANT NOTE : The commits I make are vibecoded by Claude Code and verified by myself and tested before making it as a release.

### What is the difference with the official Limine?

Limine (pronounced as demonstrated [here](https://www.merriam-webster.com/dictionary/in%20limine))
is a modern, secure, portable, multiprotocol bootloader and boot manager, also used
as the reference implementation for the [Limine boot protocol](https://github.com/Limine-Bootloader/limine-protocol/blob/trunk/PROTOCOL.md). The difference is that more features via bootctl are added.

### Releases and packages

Releases are tagged `vX.Y.Z-extra` from v12.9.3 onwards, and were tagged `vX.Y.Z-modified`
before. The loader reports the release it was built from, e.g. `Limine 12.9.3-extra`, which
is based on Limine 12.9.1.

On Arch Linux, the [`limine-extra`](https://github.com/malik05051/malik05-repo/tree/main/limine-extra)
package from the [malik05 repository](https://github.com/malik05051/malik05-repo) replaces
Arch's `limine` with these releases. It was previously named `limine-systemd-bootctl`.
