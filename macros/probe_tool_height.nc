; probe_tool_height.nc
; Intended to be used as an M6 tool change macro.
; Moves to the tool change position, pauses for tool swap, then measures
; the new tool against the ETS and applies the tool length offset.
;
#<chg_x_mpos_mm>=75.0                 ; tool change position X
#<chg_y_mpos_mm>=139.0                ; tool change position Y
#<ets_x_mpos_mm>=1.5                  ; X location of the ETS
#<ets_y_mpos_mm>=139.0                ; Y location of the ETS
#<ets_z_mpos_min_mm>=-40              ; G38 target in Z for ETS
#<probe_seek_rate_mm_per_min>=200
#<probe_feed_rate_mm_per_min>=80
#<retract_height>=3.0                 ; retract distance between seek and feed probes
#<safe_z_mpos_mm>=-1.0                ; safe Z height for XY moves

; Move to safe Z then tool change position
G53 G0 Z#<safe_z_mpos_mm>
G53 G0 X#<chg_x_mpos_mm> Y#<chg_y_mpos_mm>
G4 P0.25                              ; wait for motion to complete

(print,Please install tool number: #5400 then resume job)
M0                                    ; pause for tool change

; Move to ETS and measure new tool
G53 G0 X#<ets_x_mpos_mm> Y#<ets_y_mpos_mm>
G38.2 G53 Z#<ets_z_mpos_min_mm> F#<probe_seek_rate_mm_per_min>
G53 G1 Z[#<_abs_z>+#<retract_height>] F200
G38.2 G53 Z#<ets_z_mpos_min_mm> F#<probe_feed_rate_mm_per_min>

G53 G0 Z#<safe_z_mpos_mm>

; Apply tool length offset using stored G59 Z offset
G43.1 Z[#5063 + #5323]

(print,ETS is #5063  G59 Z offset is #5323 mm)
