# SHT3x Documentation

## Guides

| File | Purpose |
| --- | --- |
| [integration.md](integration.md) | Embedding the driver in a larger firmware: ownership boundary, cooperative flow, transport contract, presence and health. |
| [hardware.md](hardware.md) | Hardware validation coverage and the serial HIL runbook. |
| [esp-idf.md](esp-idf.md) | ESP-IDF component and example boundary, adapter contract, validation commands. |
| [reference/README.md](reference/README.md) | Vendor source documents and maintained chip notes. |

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

## Repository And Package Boundary

The tracked documentation is limited to maintained usage, integration,
hardware-validation, and protocol-reference material. Generated Doxygen,
PlatformIO, ESP-IDF, and HIL output stays ignored. Serial transcripts, run
summaries, review notes, prompts, audits, and dated reports are working artifacts,
not maintained documentation. Archive accepted evidence outside the checkout;
[hardware.md](hardware.md) records only the resulting coverage boundary.

The original Sensirion PDFs and alert spreadsheet remain under
`reference/vendor/` as maintainer source material. They are intentionally
excluded from the published PlatformIO package, which includes the maintained
guides and chip notes alongside the library, examples, and HIL tooling.
