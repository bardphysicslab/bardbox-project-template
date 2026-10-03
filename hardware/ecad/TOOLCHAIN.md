# BardBox KiCad toolchain

Use KiCad **10.0.5** with JLCImport **1.6.7** and the BardBox **Import to BardBox** action plugin. On macOS, install and check the button from the `bardbox-tools` repository using `scripts/install_kicad_bardbox_import.py`. Full instructions: `bardbox-tools/docs/kicad-bardbox-import.md`.

Open a PCB project under `hardware/ecad/<project>/` and choose **Tools → External Plugins → Import to BardBox**. The button retains the raw project-local JLCImport files and copies new symbols, footprints, and 3D models to the sibling `hardware/ecad/components/bardbox` library. Use the `bardbox` copy in designs. Existing BardBox parts are never overwritten automatically.

The graphical editor runs on the contributor's computer. A container may later run headless export and validation checks; it is not the shared desktop installation.
