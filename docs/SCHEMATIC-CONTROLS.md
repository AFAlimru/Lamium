# Schematic controls redesign

Status: design discussion, 2026-10-20; two independent reviews. **Nothing
implemented.** This note separates what the maintainer adopted from what is
still a proposal or an open question, so a prototype does not read a
recommendation as a decision.

- **Adopted**: the maintainer agreed.
- **Proposed**: recommended here, not confirmed.
- **Open**: undecided.

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

## Adopted direction

### Move target

- One move target shared by keys and the menu: placement, corner 1, corner 2,
  the save area.
- One Press action cycles the target; the looked-at selection can also set it.
- The six direction keys, the menu movement steppers and the move toasts act
  on the target. Rotate/mirror and the layer controls stay settings of the
  selected placement.
- Display: the target name in the schematic HUD when it is on, the target's
  frame or mark emphasized in the world, and the target named in move toasts.
- Reset to the placement on world/dimension change and when a placement is
  selected or placed.
- The looked-at selection extends "select the placement you look at"; the ray
  and distance rule is in Proposed below.

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
- **View slice** is one-shot: a key or menu item sets the axis from the view,
  the cut to the player's feet layer along that axis, mode to only, and keep to
  the side away from the camera. The axis stays fixed until the command runs
  again; the adjust key plus the wheel moves the cut. No continuous view sync,
  which would restart the check on every look. Details are in Proposed.
- Check and Materials read the same cut region; a change restarts the check
  once.

## Proposed rules (need confirmation)

### Storage and migration

The first proposal collided with the old format: `layerAxis` is a six-direction
integer in the old file, so a new Y/X/Z axis cannot reuse the key — an older
Lamium would read a different direction, and a string value would fall back to
the default. The new state must live under a different key.

- Add `layerView: { axis: "y"|"x"|"z", cut: <offset>, keep: "low"|"high",
  mode: "all"|"only"|"upTo" }` per placement. Leave the legacy
  `layerAxis`/`layerMode`/`layer` fields untouched for older readers.
- Keep the document at `version: 1`; older readers ignore `layerView`, and new
  readers prefer it.
- When the placed size is known, dual-write the exact legacy fields: all is
  size-independent; only or up-to low uses the low direction with
  `index = cut`; up-to high uses the high direction with
  `index = extent - 1 - cut`. An older Lamium then shows the same cells.
- Never convert without the size: a placement whose structure is missing keeps
  its legacy fields and is written back unchanged; other placements can still
  be saved. `layerView` is added when the structure loads and the placement is
  next saved.
- A mixed file must round-trip both forms. Rollback to an older Lamium reads
  the last legacy fields; a cut changed after that save cannot be represented
  there, so decide whether the cut is only editable while the size is known or
  rollback may show the previous view.
- Open: document `version: 1` plus `layerView` (proposed) versus a document
  version 2 (simpler, but an older Lamium rejects the whole file); whether the
  legacy fields are dual-written always or only until the next release.

### View slice details

- Axis: the dominant axis of the full view vector (largest |x|, |y| or |z|),
  not the 60-degree movement rule. Looking 30 degrees down at a wall still
  slices on X/Z; looking 70 degrees down slices on Y. Ties need a fixed order.
- Cut: the view source's layer along that axis: the player's feet in normal
  play, and the detached camera's block in Freelook or FreeCamera (the same
  view source as the movement direction). Clamp to the placement and toast
  when clamped. The alternative is always the body's feet.
- Mode starts as only; keep is the side away from the camera. The crosshair is
  not used: it can point at a ghost, inside a placement or outside the build,
  and building from the bottom needs a stable reference.
- A successful slice sets the remembered adjustment to the cut, so the next
  adjust plus wheel moves the cut instead of repeating an earlier rotate.
- Repeat sign: the memory stores the kind of adjustment, not the last sign;
  the wheel direction chooses the sign. After a back step, adjust + wheel up
  therefore moves forward. The toast names the value. The alternative is to
  remember the last sign as well.
- Examples: standing at Y=64 looking east slices on X at the player's X;
  looking 70 degrees down slices on Y at Y=64; FreeCamera far away looking down
  slices on Y at the camera's Y, not the body's; a 90-degree rotation swaps the
  X/Z extent and clamps the local cut.

### Repeat details

- Changing the move target or selecting another placement clears the
  remembered adjustment, so a rotate done on the placement cannot repeat after
  the target changed to a corner. A failed operation does not update it.
- Rotate, mirror and layer operations still act on the selected placement; the
  repeat toast names the operation and what it acts on.

### Keep operation

- The Layers menu needs a way to change Keep: a **Keep** stepper (low/high) as
  a sixth item, or a **Flip side** command that switches to "up to" and turns
  the kept side over. Without it the field cannot be changed from the menu, and
  the view slice's keep has no visible effect in only mode.
- Open: which form, and the Layers menu item count with the slice.

### Target validity

- Add an explicit **no target** state. Check validity before an operation and
  revalidate after a delete, area clear, visibility change or placement
  selection.
- If the current target becomes invalid, fall back to the first valid one in
  this order: placement (when selected), corner 1, corner 2, area (when both
  corners are set), else no target.
- Direction keys with no target toast that there is nothing to move; the
  target key cycles the valid targets.
- Open: whether an unset target is skipped or selected with a "not set" toast,
  and how no target is displayed.

### Looked-at selection

- Use the same drawn view ray as movement (`detachedViewRay` when a detached
  view owns the view, otherwise the player's eye and view), not the body view
  in FreeCamera.
- Compare distances first: ray entry to each placement box and to the area
  box, and the hit distance of a corner block; choose the nearest.
- Tie-break by kind: corner, then placement, then area (proposed).
- Open: the exact tie rule, and a corner that sits inside a placement.

### Target display without the HUD

- The HUD is optional, so the HUD-off case relies on the frame or mark and on
  the target and move toasts. Where the placement and area frames overlap, the
  active frame is drawn last with a brighter or thicker stroke.
- Open: whether an unset target is skipped or selected with a toast; how "no
  target" reads in the HUD when it is on.

## Open questions

- Storage: `version: 1` plus `layerView` (proposed) or document version 2; how
  long the legacy fields are dual-written; what rollback shows after a cut
  changed without the size.
- Keep operation form; Layers menu item count; whether the slice is a key, a
  menu item or both.
- Cut position in Freelook/FreeCamera (detached camera proposed, body feet the
  alternative); the dominant-axis tie order.
- Target skip versus "not set" toast; no-target display.
- Repeat sign rule (kind-only proposed; remembering the sign is the
  alternative).
- Action labels and help for target-based movement (saved action ids stay).
- Default chords while Schematics is off: accept the toast or gate them later.
- Prototype scope and branch.

## Suggested order

From the 2026-10-20 reviews, for the maintainer to confirm:

1. **L-231 first**: move target, target display and repeat cleanup with the
   current layer model, so the operation feel can be checked before the layer
   model changes.
2. **L-234 as a separate step**: settle the cut rules and the storage
   migration, and prove with pure logic that the conversion from every old
   direction and mode shows the same cells before implementing.
3. **Then inspection work**: L-183 and L-190 together where they share
   selection; L-185 stays a separate, larger task.

## Not in this round

- Item-based operation and a persistent edit mode.
- The Files/Check 3D preview's own layer peel.
- Other check or material semantics.

## Files likely touched

`Placement.h`, `PlacementStore.cpp`, `GhostBlocks.cpp`, the verification and
materials paths, `MenuModel.h`, `SchematicActions.cpp`, `SchematicsView.cpp`,
the three-language help, and `tests/SchematicTests.cpp`.

## Review

Two independent reads were returned on 2026-10-20. The second found the
`layerAxis` key collision in the first migration proposal, the missing repeat
setup after a slice, target invalidation after delete/clear, and the
distance/view-source gap in the looked-at selection; those are corrected or
recorded above. The first review's gaps (Keep operation, HUD-off display,
repeat after a target change, slice coordinate basis) are folded in too. A
prototype may live on a separate branch; main keeps today's behavior until the
design settles.
