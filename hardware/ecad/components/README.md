# BardBox KiCad components

This library contains three matched symbols and footprints:

| Symbol | Footprint |
| --- | --- |
| `bardbox:TB002-500-02BE` | `bardbox:TB002-500-02BE` |
| `bardbox:MAX31865_3648` | `bardbox:MAX31865_3648` |
| `bardbox:ESP32_S3-FEATHER_SYMBOL` | `bardbox:ESP32 S3-FEATHER` |

The terminal block also has a 3D model in `bardbox.3dshapes/`.

For a KiCad project directly under `hardware/ecad/<project>/`, register these as project libraries in that project's `sym-lib-table` and `fp-lib-table`:

```scheme
(sym_lib_table
  (version 7)
  (lib (name "bardbox") (type "KiCad") (uri "${KIPRJMOD}/../components/bardbox.kicad_sym") (options "") (descr "BardBox symbols"))
)
```

```scheme
(fp_lib_table
  (version 7)
  (lib (name "bardbox") (type "KiCad") (uri "${KIPRJMOD}/../components/bardbox.pretty") (options "") (descr "BardBox footprints"))
)
```

Keep the project at this depth so the terminal block's 3D model path resolves. If the project lives elsewhere, adjust all three relative paths together.

The MAX31865 and Feather symbols were recovered from the RKC schematic cache. Verify their pin numbering against the exact modules before fabricating a board.

The MAX31865 footprint has two plated mounting holes. Their former pad numbers 9 and 10 were cleared so they stay mechanical and do not require schematic pins. Check drill size and whether these holes should be plated or non-plated for the exact screw and standoff before fabrication.
