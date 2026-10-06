# Chassis cost tool

Shows what the T-slot chassis costs and where it can be cheaper. A standalone program for
the Windows PC (the Raspberry Pi works too). Independent of the firmware and the motor
client.

**Status:** reading the CAD is done: STEP import (main route) and the optional Fusion
script, a checker, a sample. The price catalog and the costing GUI come next, once a
real export of the base has been checked.

**Decided:** extrusions are 40 mm T-slot, single (40x40) and double (40x80), **ordered cut
to length** (priced per length plus the vendor's cut fee; no stock cutting plan needed).
Parts are McMaster-Carr; equivalent or better alternatives are welcome. The GUI must run
on the Windows PC; the Pi is optional.

```
tools/chassis_cost/
  step_import.py             STEP assembly -> parts list (the main route)
  bom_tool.py                summary, cut list, warnings; takes a STEP file or the script's JSON
  fusion/SMC_BOM_Export/     optional Fusion 360 script (+ bom_core.py, the shared logic)
  samples/                   SYNTHETIC example export (make_sample.py regenerates it)
  tests/                     pytest (STEP tests build their own STEP files)
  requirements.txt
```

## 1. Getting the CAD into the tool

The tool runs on its own and imports a file. Two ways to make that file:

| Route | What you do | Gives | Limits |
|---|---|---|---|
| **STEP (main)** | Fusion: File > Export > STEP, then open it in the tool | names, quantities through sub-assemblies, true length and cross-section of every part, volume | Fusion's Part Number field may not survive in STEP (to be checked with your file); needs the OpenCascade library |
| **Fusion script (optional)** | run `SMC_BOM_Export` inside Fusion once | the same, plus Fusion's Part Number, Description, material and mass | needs Fusion open; one click |

Both produce the same parts list; everything after the import is shared.

**Speed (measured on this PC):** a test base of 316 pieces with slotted 40x40 rails and
bolts with many grooved faces (a 1.7 MB STEP file) imports in 0.55 s; 414 simpler pieces
in 0.07 s. Real McMaster models with modelled threads are heavier, so expect seconds,
not minutes.

### Install (once)

```
cd tools\chassis_cost
py -3.12 -m venv .venv
.venv\Scripts\python -m pip install -r requirements.txt     (about 640 MB, mostly VTK)
```

On the Pi (64-bit Raspberry Pi OS): the same with `python3 -m venv .venv` and
`.venv/bin/python`; the library publishes a Linux ARM64 build.

### Use

```
.venv\Scripts\python bom_tool.py summary  base.step
.venv\Scripts\python bom_tool.py cutlist  base.step
.venv\Scripts\python bom_tool.py warnings base.step
.venv\Scripts\python bom_tool.py import   base.step      writes base_bom.json and base_bom.csv
```

### The optional Fusion script

1. In Fusion: **Utilities > Add-Ins > Scripts and Add-Ins** (Shift+S), tab **Scripts**.
2. Click **+** next to *My Scripts* and pick `tools/chassis_cost/fusion/SMC_BOM_Export`.
3. Open the base assembly, select **SMC_BOM_Export**, **Run**, choose an output folder.
4. It writes `<design>_bom.json` and `<design>_bom.csv`; `bom_tool.py` reads the JSON.

It uses only documented API calls, but has not been run inside Fusion by me; if it stops,
the dialog shows the error, please send it.

### What a parts list holds, per distinct part

`key` (vendor part number, else the name), `category`, `quantity`, `vendor`,
`vendor_part_number`, `name`, `description`, `material`, `mass_kg`, `bbox_mm` (smallest
side first), `extrusion` {`profile`, `length_mm`, `source`: name or geometry},
`attributes`, `paths` (where each piece sits), `warnings`. Units: mm and kg.

## 2. Naming conventions in Fusion (what makes extraction reliable)

1. **Vendor part number in the Part Number field and, for STEP, in the component name**
   (for example `47065T101 corner bracket`). Fusion's *Insert McMaster-Carr Component*
   already names parts this way. The part number is the primary key.
2. **Prefix the name with its category:** `EXT-` extrusion, `BRK-` bracket, `FST-` fastener,
   `PLT-` plate, `DRV-` drive (motors, mounts, wheels, hubs), `ELE-` electronics,
   `MISC-` other. Unprefixed parts are guessed from keywords.
3. **Extrusions:** `EXT-4040-<length>` or `EXT-4080-<length>` (mm), one component per
   distinct length. Unnamed rails are still recognised from their 40x40 or 40x80 section,
   at any angle; a name that disagrees with the model by more than 1 mm is flagged.
4. **Each purchased part is its own component**, so it is counted.
5. Hidden parts are counted and flagged; delete them if they are not on the build.

**Should the model carry costs?** Part numbers yes; prices no. Prices change and need a
source and a date, which the catalog keeps (section 3).

## 3. Prices: the catalog (next step)

- **Primary key:** vendor part number. **No scraping.**
- **Official APIs:** McMaster-Carr has a Product Information API for approved customers
  (request through eProcurement; client certificate plus token). No official public API
  found for Misumi or 80/20. Prices are entered by hand unless you get McMaster access.
- **Catalog CSV**, one row per vendor part: `vendor, part_number, description, unit_price,
  currency, price_breaks (qty:price;...), price_per_mm, cut_fee, min_length_mm,
  max_length_mm, lead_time_days, shipping_estimate, date_checked, source_url, status`.
  Every price starts as `status=placeholder` until you confirm it with a date.
- **Equivalence rules** per category (same profile and slot size for extrusions; same
  thread, length, head and material class for fasteners; same series and load class for
  brackets), so alternatives are compared only when they fit.
- **Whole systems are compared:** switching extrusion vendors brings in that vendor's
  T-nuts, brackets and cut fees.
- **Savings with cut-to-length:** merging nearly equal lengths (fewer distinct lengths,
  fewer order lines and cut setups), hardware alternatives, vendor comparison.

## Next

Export the base as STEP, run `bom_tool.py summary` and `warnings` on it, and send me the
STEP file (or the summary). That shows what Fusion puts in its STEP files, part numbers
especially, before the catalog and the GUI are built.
