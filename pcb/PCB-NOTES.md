# PCB Fabrication and Assembly Notes

I farmed-out PCB board fabrication to an off-shore commercial board manufaturer. I supplied these parameters during the board manufacturing process:

| Parameter | Value |
|-----------|-------|
| Material	| FR-4 TG150 |
| Size	| 108 x 63.5 mm |
| Layers |	4 |
| Thickness	| 1.6 mm	|
| Min Track/Spacing	| 8/8mil |
| Min Hole Size	 | 0.3 mm	 |
| Solder Mask	| Blue	|
| Silkscreen	| White |
| Surface Finish |	ENIG - Immersion gold 1U"
| Via Process	| Tenting vias |
| Finished Copper	(outer layers) | 2 oz Cu |
| Inner Copper	| 1 oz Cu |	
| Gold Fingers	| No	|
| "HASL"to"ENIG" |	No |


The PCB shop I ordered from also will create stanless steel solder masks for a board, which I ordered at the same time.  Given the small footprint of the TI LED driver ICs, this made laying down the solder for the reflow process much more reliable.

## Assembly

I used a reflow oven to solder components to the boards. I expect it would be difficult to hand solder the TI ICs. 

I populated the front (component) side of the board first using Pb 63/Sn 37 solder paste. I used low-temperature Bi/Sn solder for the back (LED) side.  This allowed me to used a lower temperature reflow profile for the back -- insuring the front components remained in-place during the reflow of the second side.