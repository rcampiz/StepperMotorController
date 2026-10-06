# Chassis cost tool

Shows what the T-slot chassis costs and where it can be cheaper. Independent of the
firmware and the client; nothing here touches them.

**Status:** step 1 of 3 is done: the Fusion 360 export, a sample, and a checker. The price
catalog and the costing GUI come next, after the open questions at the end are answered
and you have run the export on your model.

```
tools/chassis_cost/
  fusion/SMC_BOM_Export/     Fusion 360 script (+ bom_core.py, the tested logic)
  bom_tool.py                check an export: summary, cut list, warnings
  samples/                   SYNTHETIC example export (make_sample.py regenerates it)
  tests/                     pytest for bom_core and bom_tool (no Fusion needed)
```

## 1. Getting the data out of Fusion 360

| Route | Gives | Limits | Verdict |
|---|---|---|---|
| **API script (this folder)** | everything below, aggregated, in JSON + CSV; reads Part Number, Description, material, mass, true extrusion length via the minimum oriented box, custom attributes | needs Fusion open; one click per export | **chosen** |
| Fusion's own BOM / parts list (drawing or Manage extension) | names, quantities, part numbers, materials | no extrusion length/profile; format varies with the subscription; awkward to automate | fallback for a quick look |
| STEP export read outside Fusion (CadQuery / OCP) | geometry, assembly tree, names | Fusion's Part Number and Description are usually lost in STEP; needs a heavy CAD kernel installed | only if Fusion is unavailable |

### Run the script

1. In Fusion: **Utilities > Add-Ins > Scripts and Add-Ins** (Shift+S), tab **Scripts**.
2. Click **+** next to *My Scripts* and pick `tools/chassis_cost/fusion/SMC_BOM_Export`.
3. Open the base assembly, select **SMC_BOM_Export**, **Run**, and choose an output folder.
4. It writes `<design>_bom.json` and `<design>_bom.csv` and shows a summary.
5. Check it here: `python tools/chassis_cost/bom_tool.py summary <design>_bom.json`
   (also `cutlist`, `warnings`), then send me the JSON.

The script was written against Autodesk's documented API (BRepBody.orientedMinimumBoundingBox,
Component.partNumber/description/id, UserInterface.createFolderDialog) but has not been run
inside Fusion by me; if it stops with an error, the dialog shows the traceback, please send it.

### What it exports, per distinct part

`key` (the vendor part number, else the name), `category`, `quantity`, `vendor`,
`vendor_part_number`, `name`, `description`, `material`, `mass_kg` (per piece), `bbox_mm`
(smallest side first), `extrusion` {`profile`, `length_mm`, `source`: name or geometry},
`attributes` (the `chassis_cost` attribute group), `paths` (where each piece sits in the
assembly), `warnings`. Units: mm and kg. Schema id: `smc-chassis-bom/1`.

## 2. Naming conventions in Fusion (what makes extraction reliable)

1. **Part Number field = vendor part number** (component Properties > Part Number). It is
   the primary key; everything else is a fallback. McMaster-style numbers
   (`47065T101`) are also recognised in the component name, which is what Fusion's
   *Insert McMaster-Carr Component* produces.
2. **Prefix the component name with its category:** `EXT-` extrusion, `BRK-` bracket,
   `FST-` fastener, `PLT-` plate, `DRV-` drive (motors, mounts, wheels, hubs),
   `ELE-` electronics, `MISC-` other. Unprefixed parts are guessed from keywords.
3. **Extrusions: `EXT-<profile>-<length>`**, length in mm (or `in`):
   `EXT-1010-450`, `EXT-2020-300`, `EXT-1515-18in`. One component per distinct length
   (Fusion copies of the same component count as quantity). Unnamed rails are still
   found when their cross-section is a standard profile (20/30/40 mm series; 1010, 1020,
   1030, 1515, 1530 inch series) and they are at least 3x longer than wide. A name that
   disagrees with the model by more than 1 mm is flagged.
4. **Model each purchased part as its own component** (not a body inside a bigger one),
   so it is counted.
5. Hidden parts are counted and flagged; delete them if they are not on the build.

**Can the model carry its own cost data?** Partly. Part Number and Description are
standard component properties, so the part number lives in the model (recommended).
Prices should not: they change and need a source and a date (section 3). If you want
extra per-part data in the model anyway (e.g. `supplier`, `finish`), the script reads
API attributes in the group `chassis_cost`; Fusion's UI has no editor for those, so they
are set by a script.

## 3. Prices: the catalog (next step)

All parts come from McMaster-Carr; equivalent or better alternatives are welcome.

- **Primary key:** vendor part number. **No scraping.**
- **Official APIs:**
  - McMaster-Carr: a Product Information API exists, for approved customers only
    (request through eProcurement; client certificate plus token). It is worth asking for
    if you order regularly; otherwise prices are entered by hand.
  - Misumi and 80/20: no official public API found; prices are entered by hand.
- **Catalog CSV**, one row per vendor part: `vendor, part_number, description, unit_price,
  currency, price_breaks (qty:price;...), price_per_mm (extrusion), cut_fee, min_length_mm,
  lead_time_days, shipping_estimate, date_checked, source_url, status`.
  Every price starts as `status=placeholder` and counts as unverified until you set
  `status=confirmed` with a date.
- **Equivalence rules** per category (e.g. same profile series and slot size for
  extrusions; same thread, length, head and material class for fasteners; same series
  and load class for brackets), so alternatives are compared only when they fit.
- **Whole systems are compared, not single parts:** switching extrusion vendors drags in
  that vendor's T-nuts, brackets and cut fees.

## Open questions (needed before the catalog and the GUI)

1. **Profile system:** which T-slot series is the base built from (for example McMaster's
   1" 10-series, 1.5" 15-series, or a metric 20/30/40 mm series)?
2. **Cutting:** do you cut the extrusions yourself (then the tool plans cuts from stock
   lengths with saw kerf), or order them cut to length (then it prices per-length plus
   cut fees)?
3. **Where the GUI runs:** your Windows PC, the Raspberry Pi, or both?

Then: run the script on your base and send the `<design>_bom.json`.
