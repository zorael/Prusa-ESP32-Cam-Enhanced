## What this changes, and why

<!-- If it fixes a bug, describe the failure it fixes, not just the code you touched. -->

## Testing

- [ ] `pio run -e ai_thinker` succeeds
- [ ] Flashed and run on hardware — board: <!-- e.g. AI Thinker ESP32-CAM -->
- [ ] If it touches `webpage/`: regenerated `src/WebPage_gz.h`
      (`cd webpage && python webpage_gz_generator.py`)

Flash usage after the change: <!-- the build prints it; it sits near 79% -->

<!--
If you could not test on hardware, say so here rather than leaving the box
unticked — it changes how the change gets reviewed, and that is fine.
-->
