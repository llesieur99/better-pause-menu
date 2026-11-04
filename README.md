# Better Pause Menu

Better Pause Menu 0.6.2 turns **Pause → Options → QoL Hub** into the central in-game interface for the local QoL mods.

The hub uses OpenRAC's native RAC1 menu UI. Controller shortcut combinations are no longer required for the integrated tools. The status information is part of the same game menu frame rather than a custom overlay or separate Status page.

## Integrated pages

- **Live status** — current planet, health, bolts, deaths, checkpoint state, registered-mod count and latest progress are always visible in the native footer as soon as the hub opens.
- **Collectibles** — total Gold Bolts plus remaining Gold Bolts, Infobots and skill-point triggers in the current level.
- **Missions** — current planet, lightweight objective and latest unlock/item progress.
- **Fast Travel** — travel to planets already visited during the current session.
- **Checkpoints** — create and restore the current level's temporary quick checkpoint.
- **Save Manager** — inspect the real save slots and, on OpenRAC API 1.11+, save the active slot.
- **Gameplay Tools** — restart the current planet, skip the current in-level cutscene and view the session death count.
- **Vendor Info** — current bolt wallet and last acquired item.

A warm quick checkpoint is also created automatically shortly after a level becomes playable, matching the previous Checkpoint Plus behavior.

## Mod consolidation

The hub replaces the separate hotkey-driven UI roles of:

- Better HUD
- Collectible Tracker
- Mission Tracker
- Fast Travel
- Checkpoint Plus
- Save Manager
- Death Improvements
- Improved Vendor

Background-only mods such as Accessibility Pack, Cutscene Manager and New Game Plus Improvements remain separate because their behavior is continuously applied rather than opened through a shortcut.

Modern Controls and Multiplayer remain separate native menu integrations.

Built for OpenRAC RAC1 PAL/NTSC-U. AI-assisted implementation. GPL-3.0-or-later.

## In-game settings

The QoL Hub now includes a **Settings** page. Each integrated page can be enabled or disabled independently, automatic warm checkpoints can be switched on or off, and one action restores the default configuration. Settings apply immediately for the running game session.


## Distribution

No game assets or disc files are included. The mod uses the OpenRAC SDK and the player's own installed game data at runtime.
