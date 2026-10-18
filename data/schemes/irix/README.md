# IRIX desktop color schemes

The palettes of the 30 color schemes that IRIX 6.5 offers in its desktop
Customize panel (Gotham, Lascaux, Potrero, ...), one file per scheme, as
data for ttr's own themes. They were read from
`/usr/lib/X11/schemes/<Name>/BaseColorPalette` on an IRIX 6.5 installation
(running in the IRIS emulator) on 2026-10-18. Only the color values were
taken; SGI's files, comments and scheme descriptions were not copied.

## File format

Each `<Name>.ad` is an X resource file, so `XrmGetFileDatabase()` reads it
as is:

    isDark: true
    gamma1_0.BasicBackground: #585858
    gamma1_7.BasicBackground: #2a2a2a
    gamma2_4.BasicBackground: #141414

- `isDark` is the scheme's own dark/light flag.
- Every role appears once per display gamma: `gamma1_0`, `gamma1_7` and
  `gamma2_4`. SGI picked the three values so that a scheme looks the same
  whatever gamma the display hardware applies.

### Which gamma to use

- **Linux / any X server with an identity gamma ramp: `gamma1_0`.** These
  are the colors as they appeared on screen on a stock SGI.
- IRIX: the value for the system gamma (`gamma` command, default 1.7, so
  usually `gamma1_7`). The hardware ramp brightens them to the `gamma1_0`
  look.

## Roles

59 roles in most schemes. Twelve older schemes (Gainsborough, Lascaux,
Leonardo, Monet, Print, Rembrandt, Sargent, Titian, Turner, VanGogh,
Whistler, Willis) have 52: they lack `LinkForegroundColor`,
`ActiveLinkForegroundColor`, `VisitedLinkForegroundColor`, `ReferenceText`
and the three `PixmapButton*` roles. Take those from Base.

How SGI's desktop applies the roles (summarised from the scheme spec files
`Base/Base` and `Base/4DWmSpec`):

| Role | Used for |
|---|---|
| `BasicBackground` | `*background`: every widget not listed below, menus |
| `TextForeground` | `*foreground`, menu text, scale labels |
| `ButtonBackground` | push buttons, drawn buttons, option menu buttons |
| `TextFieldBackground` | text fields and single-line `XmText` |
| `TextBackground` | `XmText` inside a scrolled window (multi-line text) |
| `ReadOnlyBackground` | read-only text |
| `ScrolledListBackground` | `XmList` |
| `ScrollBarTroughColor` | scroll bar background (trough) |
| `ScrollBarControlBackground` | scroll bar slider (scroll bar foreground) |
| `DrawingAreaBackground` | `XmDrawingArea`, graph and overview views |
| `TextSelectedBackground` / `TextSelectedForeground` | text and list selection |
| `SelectFillColor` | toggle and radio fill (`*selectColor`) |
| `CheckColor`, `RadioColor`, `IndicatorBackground`, `LightRadioFillColor`, `DisabledCheckColor`, `IndicatorLightColor` | toggle indicators |
| `DisabledTextForeground` | insensitive text |
| `WMBackground` | window manager: frame and icon background |
| `WMActiveBackground` | window manager: focused frame and icon |
| `WMForeground` / `WMActiveForeground` | window manager: title text, normal and focused |
| `ErrorColor`, `WarningColor`, `InformationColor` | message dialogs |
| `RedColor` ... `PurpleColor`, `HighlightColor1-8`, `AlternateBackground1-6`, `DrawingAreaContrastColor1-4` | application accents |
| `LinkForegroundColor`, `VisitedLinkForegroundColor`, `ActiveLinkForegroundColor`, `ReferenceText` | help viewer links |
| `PixmapButtonFillColor`, `PixmapButtonTopShadowColor`, `PixmapButtonBottomShadowColor` | icon buttons |

Notes for a window manager:

- 4Dwm takes the frame and icon backgrounds from `WMBackground` and
  `WMActiveBackground` and leaves the title foregrounds empty in its spec,
  so Motif derives them; the `WMForeground` roles are there for clients
  that want them. Its feedback window's bottom shadow is plain black.
- Shadows, arm and select colors are never in the palette. Motif derives
  them from each background (`XmGetColors`), which is what ttr's
  `motif_colors()` does, so a frame drawn from `WMBackground` matches.

## Schemes

`bg` is `gamma1_0.BasicBackground`, `active frame` is
`gamma1_0.WMActiveBackground`.

| Scheme | | bg | active frame | roles |
|---|---|---|---|---|
| Arizona | dark | `#868373` | `#968d4f` | 59 |
| Base | light | `#d9d9d9` | `#c6c1aa` | 59 |
| Bayou | dark | `#80727c` | `#b69dab` | 59 |
| BlackAndWhite | light | `#ffffff` | `#000000` | 59 |
| Buckingham | dark | `#795f78` | `#9991a6` | 59 |
| Gainsborough | light | `#aaaacb` | `#aea9c2` | 52 |
| Gotham | dark | `#585858` | `#aba68c` | 59 |
| GrayScale | light | `#d9d9d9` | `#d6d6d6` | 59 |
| Inverness | light | `#aea4b9` | `#b7a5d0` | 59 |
| KeyWest | light | `#d3b5b5` | `#e3a6a6` | 59 |
| Lascaux | light | `#d9d9d9` | `#c6c1aa` | 52 |
| Leonardo | dark | `#80727c` | `#b69dab` | 52 |
| Mendocino | dark | `#5f7a80` | `#44848c` | 59 |
| Metropolis | dark | `#8c687e` | `#925279` | 59 |
| Milan | light | `#bdb9a3` | `#c6bf92` | 59 |
| Monet | light | `#b688a5` | `#aaa5d5` | 52 |
| Pacific | dark | `#396375` | `#0e7698` | 59 |
| Potrero | dark | `#21628c` | `#b38eb4` | 59 |
| Print | light | `#c1c1c1` | `#d6d6d6` | 52 |
| RedGreenSafe | light | `#aaaacb` | `#aea9c2` | 59 |
| Rembrandt | dark | `#5f7a80` | `#44848c` | 52 |
| Rio | dark | `#216a97` | `#e788e8` | 59 |
| RoseGarden | light | `#b688a5` | `#aaa5d5` | 59 |
| Sargent | dark | `#3a6476` | `#909585` | 52 |
| Titian | light | `#c4c4c4` | `#c6c1aa` | 52 |
| Turner | dark | `#21628c` | `#b38eb4` | 52 |
| Vancouver | light | `#95a5bd` | `#b0bad7` | 59 |
| VanGogh | dark | `#795f78` | `#9991a6` | 52 |
| Whistler | dark | `#585858` | `#aba68c` | 52 |
| Willis | light | `#ffffff` | `#000000` | 52 |

- **Base is the classic Indigo Magic look.** IRIX's `IndigoMagic` scheme
  directory only points to Base, and Lascaux (SGI's default) has the same
  colors in these roles.
- Several 52-role schemes share their main colors with a 59-role one:
  Whistler/Gotham, Turner/Potrero, Rembrandt/Mendocino, VanGogh/Buckingham,
  Leonardo/Bayou, Willis/BlackAndWhite.

## Fonts

The schemes share one font palette (`Base/FontPalette`): Helvetica bold for
labels and titles, Helvetica medium for plain labels, Helvetica bold oblique
for emphasis and SGI's `screen` font for fixed width. Pixel sizes, normal
(small-fonts variant in brackets): extra large bold 20 (17), large bold 17
(14), bold 14 (12), small bold 12 (11), tiny bold 10 (10), plain 14 (12),
small plain 12 (10), oblique 14 (12), small oblique 12 (10), fixed 15 (13),
small fixed 13 (11).
