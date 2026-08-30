# SHT3x Reference Material

Source material for the driver's protocol, timing, and alert behaviour.

- [sht3x-chip-notes.md](sht3x-chip-notes.md) — the working notes: command table,
  timing, CRC, status bits, alert-limit packing, reset behaviour, the vendor
  source inventory, and the known inconsistencies between vendor documents.
- `vendor/` — the original Sensirion PDFs and the alert bit-conversion
  spreadsheet, kept verbatim. These are the authority whenever exact wording,
  figures, drawings, or legal notices matter.

The vendor files are source material for maintainers, not package payload; they
are excluded from the published library package.

Hardware validation evidence lives in [../hardware.md](../hardware.md), not here.
