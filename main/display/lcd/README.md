# XiaoZhi LCD display modules

The reusable public API remains in `../lcd_display.h`.

- `lcd_display_core.cc`: fonts, themes, constructors, teardown, and locking.
- `lcd_display_chat.cc`: both chat layout implementations.
- `lcd_display_controls.cc`: control center, overlays, and recent applications.
- `lcd_display_conversation.cc`: messages, preview images, zoom, and bottom nav.
- `lcd_display_dashboard.cc`: idle dashboard.
- `lcd_display_wifi.cc`: Wi-Fi modal, scanning, credentials, and connection flow.
- `lcd_display_rendering.cc`: status, emotions, emoji collections, and themes.
- `lcd_display_music.cc`: music visualizer lifecycle and progress.

`lcd_display_internal.h` is private to the implementation. Board-specific UI
must remain under its board directory rather than being added here.
