# PCB Fabrication and Assembly Notes

V7 of the PCB is the current version.  PCBWay generously offered to sponsor me on this latest version of the board. I supplied these parameters during the PCB manufacturing process:

| Parameter | Value |
|-----------|-------|
| Material	| FR-4 TG150 |
| Size	| 105.5 x 63.5 mm |
| Layers |	4 |
| Thickness	| 1.6 mm	|
| Min Track/Spacing	| 8/8mil |
| Min Hole Size	 | 0.3 mm	 |
| Solder Mask	| Green	|
| Silkscreen	| White |
| Surface Finish |	ENIG - Immersion gold 1U"
| Via Process	| Tenting vias |
| Finished Copper	(outer layers) | 2 oz Cu |
| Inner Copper	| 1 oz Cu |	
| Gold Fingers	| No	|
| "HASL"to"ENIG" |	No |


If you are planning to assemble the board yourself, I'd suggest using a 0.1mm stainless steel solder stencil along with Type4 or Type 5 solder paste.  I did just that in my early prototypes and, given the small footprint of the TI LED driver ICs, this made laying down the solder for the reflow process much more reliable.

## Assembly

I used a reflow oven to solder components to the boards. I expect it would be difficult to hand solder the TI ICs. 

I populated the front (IC component) side of the board first using Pb 63/Sn 37 solder paste. I used low-temperature Bi/Sn solder for the back (LED) side.  This allowed me to used a lower temperature reflow profile for the back -- insuring the front components remained in-place during the reflow of the second side.