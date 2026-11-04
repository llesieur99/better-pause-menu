# Better Pause Menu

Better Pause Menu 0.7.0 turns **Pause → Options → QoL Hub** into the central in-game interface for the local QoL mods.

The hub uses OpenRAC's native RAC1 menu UI. Controller shortcut combinations are no longer required for the integrated tools. The status information is part of the same game menu frame rather than a custom overlay or separate Status page.

## Integrated pages

- **Live status** — current planet, health, bolts, deaths, checkpoint state, registered-mod count and latest progress are always visible in the native footer as soon as the hub opens.
- **Collectibles** — total Gold Bolts plus remaining Gold Bolts, Infobots and skill-point triggers in the current level.
- **Missions** — current planet, lightweight objective and latest unlock/item progress.
- **Fast Travel** — travel to planets already visited during the current session.
- **Checkpoints** — create and restore the current level's temporary quick checkpoint.
- **Save Manager** — inspect the real save slots and, on OpenRAC API 1.11+, save the active slot.
- **Gameplay Tools** — restart the current planet and view the session death count. Cutscene handling stays in the dedicated background safety module.
- **Vendor Info** — current bolt wallet and last acquired item.

A warm quick checkpoint is also created automatically shortly after a level becomes playable, matching the previous Checkpoint Plus behavior.

## Mod consolidation

The hub absorbs the separate hotkey/UI roles of **Better HUD, Collectible Tracker, Mission Tracker, Checkpoint Plus, Death Improvements and Improved Vendor**. Those legacy UI mods should stay disabled when Better Pause Menu is enabled.

**Fast Travel** and **Save Manager** are integrated into the Hub but deliberately remain maintained as standalone packages too. Players can therefore install either one without installing the full Hub.

**Accessibility Pack, Cutscene Manager and New Game Plus Improvements** remain separate background modules because their behavior runs continuously. Their current status/configuration is shown inside the Hub.

Modern Controls and Multiplayer remain separate native menu integrations.

Built for OpenRAC RAC1 PAL/NTSC-U. AI-assisted implementation. GPL-3.0-or-later.

## In-game settings

The QoL Hub includes a **Settings** page. Automatic warm checkpoints can be switched on or off for the running session. Fast Travel and Save Manager are identified there as dual Hub/standalone features. Background-module values remain controlled by Launcher → Mods and are displayed read-only inside the Hub.


## Distribution

No game assets or disc files are included. The mod uses the OpenRAC SDK and the player's own installed game data at runtime.


## 12-module consolidation

The QoL Hub is now the central UI for the 12 QoL projects created together:

- Better Pause Menu — the hub itself.
- Better HUD — absorbed into the always-visible Hub status.
- Collectible Tracker — absorbed; the main Hub row already shows the live Gold Bolt total.
- Mission Tracker — absorbed into Missions.
- Checkpoint Plus — absorbed into Checkpoints and the automatic warm checkpoint.
- Death Improvements — absorbed into Gameplay Tools and session death statistics.
- Improved Vendor — absorbed into Vendor Info.
- Fast Travel — integrated into the Hub **and remains a standalone mod/package**.
- Save Manager — integrated into the Hub **and remains a standalone mod/package**.
- Accessibility Pack — continues as a background mod; its live configuration/status is visible in the Hub.
- Cutscene Manager — continues as a background safety mod; its status is visible in the Hub.
- New Game Plus Improvements — continues as background logic; its status is visible in the Hub.

The absorbed UI-only mods no longer need to be enabled alongside Better Pause Menu. Keeping them disabled avoids duplicate hotkeys, duplicate pages and duplicate hooks. Fast Travel and Save Manager are intentionally still maintained separately for players who want only those features.
