# Nova OS display modules

The public API remains in `../nova_display.h`. Implementation is split by
responsibility so board integration does not depend on implementation details.

- `nova_display_core.cc`: lifecycle, theme state, common widgets, and navigation.
- `nova_display_home.cc`: home, application launcher, and quick settings.
- `nova_display_connectivity.cc`: Wi-Fi and Bluetooth connection flows.
- `nova_display_dialogs.cc`: reusable settings and application dialogs.
- `nova_display_appearance.cc`: display and appearance settings pages.
- `nova_display_audio_storage.cc`: audio, storage, files, and battery pages.
- `nova_display_system.cc`: region, system, Chronos, and PinLab pages.
- `nova_display_media.cc`: page dispatch, live updates, and media/download dialogs.
- `nova_display_apps.cc`: calculator and radio applications.
- `nova_game_flappy.cc`: Flappy game.
- `nova_games_classic.cc`: 2048 and Snake games.
- `nova_games_arcade.cc`: Brick, Minesweeper, and Racer games.
- `nova_display_creative.cc`: piano, sketchpad, and resource monitor.
- `nova_display_chronos.cc`: Chronos notifications, navigation, music, weather,
  and phone views.

`nova_display_internal.h` is private to these implementation files. New public
operations belong in `nova_display.h`; shared implementation-only declarations
belong in the internal header. Keep hardware operations behind `NovaCallbacks`
instead of including the concrete board class.
