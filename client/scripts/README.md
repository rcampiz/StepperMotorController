# Ad-hoc bench scripts

Moved out of `src/` so pytest does not collect them and so they are not mistaken
for the application. They predate the SCPI command set: `test_raw_commands.py`,
`serial_diag.py` and `test_encoder.py` still send flat names (`PING`,
`GET_STATUS`, `SET_FORMAT`) that the current firmware rejects, and all five
hard-code `COM6`. Use `scripts/estop_baseline.py` at the repository root for
the Phase 4 baseline; these will be replaced by core scenarios once the shared
client core exists.
