# Chronos display modules

The public `ChronchiDisplay` API remains in `../chronchi_display.h`.

- `chronchi_display_core.cpp`: lifecycle, navigation bars, overlays, recent apps,
  and resolution handling.
- `chronchi_display_navigation.cpp`: navigation icon rendering.
- `chronchi_display_screens.cpp`: construction of watch, QR, notification,
  weather, music, status, and alert screens.
- `chronchi_display_theme.cpp`: synchronized theme and color application.
- `chronchi_display_updates.cpp`: screen selection and live data updates.

Shared implementation-only declarations are kept in
`chronchi_display_internal.h`. Bluetooth transport and alert behavior remain in
their existing sibling modules and must not be moved into the display layer.
