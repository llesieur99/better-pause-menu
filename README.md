# Better Pause Menu / QoL Hub

Better Pause Menu turns **Pause → Options → QoL Hub** into the central native RAC1 interface for quality-of-life tools.

It uses OpenRAC's native menu UI. It does not replace the game's original preview image and it does not add a duplicate gameplay bolt/health overlay.

## Integrated features

- **Status** — current planet, health, deaths, checkpoint state and latest progress.
- **Collectibles** — Gold Bolts, local remaining Gold Bolts, Infobots and skill-point triggers.
- **Missions** — lightweight current objective and recent progress events.
- **Fast Travel** — travel directly from the pause menu to unlocked planets.
- **Checkpoints** — temporary quick checkpoint create/restore plus optional warm auto-checkpoint.
- **Save Manager** — inspect real save slots and, on OpenRAC API 1.11+, save the active slot.
- **Gameplay Tools** — restart the current planet and view session deaths.
- **Vendor Info** — current wallet and last acquired item.
- **Accessibility status** — reads Accessibility Pack configuration.
- **Cutscene Manager status** — reads the safe cutscene background module configuration.

## Consolidation

The Hub absorbs the in-game UI/functionality of these earlier standalone projects:

- Better HUD
- Collectible Tracker
- Mission Tracker
- Checkpoint Plus
- Death Improvements
- Improved Vendor

Those should stay disabled when the Hub is enabled.

**Fast Travel** and **Save Manager** are also integrated, but remain maintained as standalone packages for players who want only those features.

**Accessibility Pack** and **Cutscene Manager** remain separate background modules because their runtime behavior must continue outside the menu.

## Safety

- No game assets or disc files are included.
- Level-ending/progression cutscenes are not skipped by the Hub.
- Fast Travel only exposes planets that are already unlocked.
- Save writes target only the game's active save slot.
- The player's own installed game data is used at runtime.

Built for OpenRAC RAC1 PAL/NTSC-U. AI-assisted implementation. GPL-3.0-or-later.
