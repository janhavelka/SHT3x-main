# SHT3x Documentation

## Guides

| File | Purpose |
| --- | --- |
| [integration.md](integration.md) | Embedding the driver in a larger firmware: ownership boundary, cooperative flow, transport contract, presence and health. |
| [hardware.md](hardware.md) | Hardware validation coverage and the serial HIL runbook. |
| [esp-idf.md](esp-idf.md) | ESP-IDF component and example boundary, adapter contract, validation commands. |
| [reference/README.md](reference/README.md) | Vendor source documents and the working chip notes. |
| [open-issues.md](open-issues.md) | Confirmed defects and simplifications not yet fixed, with a concrete proposal for each. |

The root [README](../README.md) covers installation, the API surface, and usage.

## API Reference

Public API comments live in `include/SHT3x/`. From a full repository checkout:

```bash
doxygen Doxyfile
```

Output lands in the ignored `.doxygen/html/index.html`. Extraction is limited to
documented entities, and undocumented public symbols, incomplete parameter or
return documentation, malformed references, and any other Doxygen warning fail
the build.

`python tools/check_docs_contract.py` additionally rejects broken local Markdown
links, tracked build/run scratch artifacts, and weakened strict-Doxygen
settings.

## What Belongs Here

Evergreen guides only. Generated HIL run directories, serial transcripts, run
summaries, and dated validation reports are disposable local output — they are
git-ignored and archived outside the checkout if they are worth keeping.
[hardware.md](hardware.md) records which behaviours a run moved from "not run"
to "covered"; it does not accumulate run artifacts.

[open-issues.md](open-issues.md) is the one exception, and it is a *live*
backlog rather than a report: entries are deleted as they are fixed, and the fix
is recorded in the changelog. A review write-up that only describes finished work
does not belong here — archive it outside the checkout or leave it in the pull
request that did the work.

## Package Boundary

The published PlatformIO package is library-focused. It contains the source,
public headers, examples, README/changelog, component metadata, the
package-facing guides, the chip notes, and the host-side HIL runner with its CLI
contract module. The open-issues backlog ships with it, so a
consumer pinning a commit can see that commit's known defects. It excludes local run output, build output, Doxygen output, and
the bulky vendor PDFs and spreadsheet, which are maintainer source material.
