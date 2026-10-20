# Schematic controls redesign

Status: design discussion, 2026-10-20. **Nothing implemented.** This note
records the direction the maintainer chose and the questions that are still
open, so the plan can be reviewed with fresh eyes before a prototype.
[SCHEMATIC.md](SCHEMATIC.md) still describes the behavior in the game today;
[BACKLOG.md](BACKLOG.md) owns task status (L-231 controls, L-234 layer model).

## Problems seen in game

- The six direction keys always move the selected placement, while the radial
  menu moves a separate move target (placement, corner 1, corner 2, save
  area). The same directions mean different things on the two surfaces.
- Corner and area targets cannot be moved from keys, and the move target is
  invisible once the menu closes.
- "Repeat the last adjustment" records only menu steppers and includes the
  layer axis and mode, so a wheel turn can change the layer model by accident.
- Layers are six combined directions plus a mode and a zero-based index from
  the chosen side. The direction decides both the numbering origin and the
  "up to" side, which is hard to read and does not fit a view-based control.
- The radial menu's Area category holds seven items, more than any other
  child category.
- Every schematic key is unbound, so the quick paths are hard to find.

## Decisions

### Move target

- One move target shared by keys and the menu: placement, corner 1, corner 2,
  the save area.
- One Press action cycles the target; the looked-at selection can also set it.
- The six direction keys, the menu movement steppers and the move toasts act
  on the target. Rotate/mirror and the layer controls stay settings of the
  selected placement.
- Display: the target name in the schematic HUD, the target's frame or mark
  emphasized in the world, and the target named in move toasts.
- Reset to the placement on world/dimension change and when a placement is
  selected or placed.
- The looked-at selection extends "select the placement you look at": a corner
  block first, then a placement box, then a save-area box.

### Repeat the last adjustment

- Repeats movement, rotate/mirror and the layer cut only. Excludes the layer
  axis/mode, placement selection and the move target.
- Key actions update the remembered adjustment, not only menu steppers, so
  the adjust key is consistent across both surfaces.

### Corners and default keys

- Corner 1 and corner 2 keep separate actions. A setter is often re-run after
  the view moves, so merging them would hurt.
- Default binding only for forward/back: Alt + Wheel up/down. No corner
  defaults and no edit mode in this round.
- While Schematics is off, the default chord is still consumed and shows the
  turn-it-on toast. Accepted for now; gating it on the feature switch remains
  an option.

### Layers: a cut plane

- Replace the six-direction model with **Axis** (Y / X / Z), **Cut**,
  **Keep** (low/high) and **Mode** (all / only / up to).
- **Cut** is stored as an offset from the placement's low corner along the
  axis, so moving the placement keeps the same slice. The screen shows the
  world coordinate (origin + offset) and the index within the extent. This is
  why the world coordinate is only a display: a world-space cut would be left
  behind when the placement moves.
- **Keep** is used by "up to": low shows cells at or below the cut, high at or
  above. "Only" shows the one layer at the cut; "all" ignores the cut.
- **View slice** is one-shot: a key or menu item sets the axis from the
  dominant view axis, the cut at the looked-at block (falling back to the
  player's layer), mode to only, and keep to the side away from the camera.
  The axis stays fixed until the command runs again; the adjust key plus the
  wheel moves the cut. There is no continuous view sync, which would restart
  the check on every look.
- Check and Materials read the same cut region; a change restarts the check
  once.
- Old placements (six directions + index + mode) convert when their structure
  loads: `cut = index` with keep low, or `cut = extent - 1 - index` with keep
  high. They save in the new form after conversion.

## Open questions

- Target display details and reset rules; precedence when a placement and the
  area overlap under the crosshair.
- Cut offset when a rotation swaps the extent: the clamp rule.
- Migration format/version and when the new fields are first written.
- Layers menu: five items (axis / cut / mode / cut here / show all) or six
  with the view slice; whether the slice is a key, a menu item or both.
- Action labels and help for target-based movement (saved action ids stay).
- Default chords while Schematics is off: accept the toast or gate them later.
- Whether the one-shot slice matches the intended workflow (axis at command
  time, adjust key plus wheel, no continuous sync).
- Prototype scope and branch.

## Not in this round

- Item-based operation and a persistent edit mode.
- The Files/Check 3D preview's own layer peel.
- Other check or material semantics.

## Files likely touched

`Placement.h`, `PlacementStore.cpp`, `GhostBlocks.cpp`, the verification and
materials paths, `MenuModel.h`, `SchematicActions.cpp`, `SchematicsView.cpp`,
the three-language help, and `tests/SchematicTests.cpp`.

## Review

An independent read is requested before a prototype. A prototype may live on
a separate branch; main keeps today's behavior until the design settles.
