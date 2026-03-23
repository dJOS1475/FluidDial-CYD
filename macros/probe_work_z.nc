; probe_work_z.nc
; Sets the work Z 0 at the tip of the probe.
; Also finds the tool sensor activation point and calculates the Z difference
; between work 0 and the toolsetter. The offset is stored in G59 Z so future
; M6 macros can use it.
;
; !!! Before running this, move the probe over the work !!!
;
#<ets_x_mpos_mm>=1.5                   ; X location of the ETS
#<ets_y_mpos_mm>=139.0                 ; Y location of the ETS
#<ets_z_mpos_min_mm>=-40               ; G38 target in Z for ETS
#<probe_seek_rate_mm_per_min>=200
#<probe_feed_rate_mm_per_min>=80
#<work_probe_min_mm>=-35               ; probe max G53 travel in Z
#<retract_height>=3.0                  ; retract distance between seek and feed probes
#<safe_z_mpos_mm>=-1.0                 ; safe Z height for XY moves
#<was_metric>=#<_metric>

G21                                    ; all moves in mm
G49                                    ; reset the TLO

; Set work Z 0 at the probe tip
G38.2 G53 Z#<work_probe_min_mm> F#<probe_seek_rate_mm_per_min>
G53 G1 Z[#<_abs_z>+#<retract_height>] F200
G38.2 G53 Z#<work_probe_min_mm> F#<probe_feed_rate_mm_per_min> P0  ; slow probe, sets work 0
#<z_mpos_mm>=#5063                     ; save the work mpos

G53 G0 Z#<safe_z_mpos_mm>

; Move to ETS and find Z in mpos
G53 G0 X#<ets_x_mpos_mm> Y#<ets_y_mpos_mm>
G38.2 G53 Z#<ets_z_mpos_min_mm> F#<probe_seek_rate_mm_per_min>
G53 G1 Z[#<_abs_z>+#<retract_height>] F200
G38.2 G53 Z#<ets_z_mpos_min_mm> F#<probe_feed_rate_mm_per_min>

; Calculate and store the offset
#<ets_offset_mm>=[#5063 - #<z_mpos_mm>]
G10 L2 P6 Z#<ets_offset_mm>           ; store offset in G59

G53 G0 Z#<safe_z_mpos_mm>

; Restore imperial mode if it was active
o100 if [#<was_metric> EQ 0]
  G20
o100 endif

(print,ETS offset from work Z is #<ets_offset_mm> mm)
