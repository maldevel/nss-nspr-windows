# nss-nspr-windows

Mozilla NSS and NSPR set up to be built for Windows (Vista and later) by Visual Studio 2017.

**Network Security Services (NSS)** is a set of libraries designed to support cross-platform development of security-enabled
client and server applications. For more information, see https://developer.mozilla.org/en-US/docs/Mozilla/Projects/NSS.

**Netscape Portable Runtime (NSPR)** provides a platform-neutral API for system level and libc-like functions
and is required to build NSS. See https://developer.mozilla.org/en-US/docs/Mozilla/Projects/NSPR.

This repository contains following stable releases:
* NSPR 4.17
* NSS 3.34

Sources were patched to fix minor Windows-specific build errors. 

## Required software

1. Visual Studio 2017 with Windows 8.1 or 10 SDK (not tested with other versions)
2. Mozilla Build suite: https://wiki.mozilla.org/MozillaBuild

# Usage

1. In **build.cmd**, change the paths to Visual Studio helper files and Mozilla Build suite (if your paths differ from defaults).
2. Run **build.cmd**.
3. Run **test.cmd** if you want to run tests (can take a very long time).