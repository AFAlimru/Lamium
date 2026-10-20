# Schematic controls redesign

Status: design discussion, 2026-10-20, reviewed once. **Nothing implemented.**
This note records the direction the maintainer chose and the questions that are
still open, so the plan can be checked again before a prototype.
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
- Display: the target name in the schematic HUD when it is on, the target's
  frame or mark emphasized in the world, and the target named in move toasts.
  The HUD is optional, so the HUD-off case relies on the frame/mark and the
  target/move toasts. Where the placement and area frames overlap, the active
  frame is drawn last with a brighter or thicker stroke.
- Cycle only valid targets: the placement when one is selected, a corner when
  it is set, and the area when both corners are set. Whether an unset target is
  skipped or selected with a "not set" toast is still open.
- Reset to the placement on world/dimension change and when a placement is
  selected or placed.
- The looked-at selection extends "select the placement you look at": a corner
  block first, then a placement box, then a save-area box.

### Repeat the last adjustment

- Repeats movement, rotate/mirror and the layer cut only. Excludes the layer
  axis/mode, placement selection and the move target.
- Key actions update the remembered adjustment, not only menu steppers, so
  the adjust key is consistent across both surfaces.
- Changing the move target or selecting another placement clears the
  remembered adjustment, so a rotate done on the placement cannot repeat after
  the target changed to a corner. A failed operation does not update it.
- Rotate, mirror and layer operations still act on the selected placement; the
  repeat toast names the operation and what it acts on.

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
  above. "Only" shows the one layer at the cut; "all" ignores the cut. The
  Layers menu needs an operation for it: a **Keep** stepper (low/high) as a
  sixth item, or a **Flip side** command that switches to "up to" and turns
  the kept side over. Without it the model field cannot be changed from the
  menu, and the view slice's keep has no visible effect in only mode.
- **View slice** is one-shot. The axis is the dominant axis of the full view
  vector (largest |x|, |y| or |z|), not the 60-degree movement rule: looking 30
  degrees down at a wall still slices on X/Z, while looking 70 degrees down
  slices on Y. The cut is the view source's layer along that axis: the player's
  feet in normal play, and the detached camera's block in Freelook or
  FreeCamera (the same view source as the movement direction), clamped to the
  placement with a toast when it was. The mode starts as only, and keep is the
  side away from the camera. The crosshair is deliberately not used: it can
  point at a ghost, inside a placement or outside the build, and building from
  the bottom needs a stable reference. The axis stays fixed until the command
  runs again; the adjust key plus the wheel moves the cut. No continuous view
  sync, which would restart the check on every look. Examples: standing at
  Y=64 looking east slices on X at the player's X; looking 70 degrees down
  slices on Y at Y=64; FreeCamera far away looking down slices on Y at the
  camera's Y, not the body's; a 90-degree rotation swaps the X/Z extent and
  clamps the local cut.
- Check and Materials read the same cut region; a change restarts the check
  once.

### Storage and migration (proposed; decide before the L-234 prototype)

The review's concern: converting an old high-side direction needs the placed
size, while the save path writes the whole placement set, so an unrelated
edit can write a placement that is not converted yet. Proposal:

- Keep the document at `version: 1` and add a per-placement
  `layerVersion: 2` with `layerAxis` (y/x/z), `layerCut` (local offset),
  `layerKeep` (low/high) and the existing `layerMode`. Older readers ignore
  unknown keys.
- When the size is known, also write the exact legacy fields for an older
  Lamium: all is size-independent; only or up-to low uses the low direction
  with `index = cut`; up-to high uses the high direction with
  `index = extent - 1 - cut`.
- Never convert without the size: a placement whose structure is missing
  keeps its legacy fields and is written back unchanged; other placements can
  still be saved. The cut fields are added when the structure loads and the
  placement is next saved.
- The new reader prefers `layerVersion: 2`; otherwise it reads the legacy
  fields and converts lazily when the size is available.
- A mixed file must round-trip both forms. Rollback to an older Lamium reads
  the last legacy fields; a cut changed after that save cannot be represented
  there, so decide whether the cut is only editable while the size is known or
  rollback may show the previous view.
- Open: version 1 plus a per-placement tag (proposed) or document version 2
  (simpler, but an older Lamium rejects the whole file); whether the legacy
  fields are dual-written always or only until the next release.

## Open questions

- Target display details and reset rules; precedence when a placement and the
  area overlap under the crosshair; whether an unset target is skipped or
  selected with a toast.
- Whether clearing the remembered adjustment on a target change is the right
  rule, and whether failed operations should leave the previous memory.
- Cut offset when a rotation swaps the extent: the clamp rule.
- Storage: version 1 plus a per-placement tag (proposed) or document version 2;
  how long the legacy fields are dual-written; what rollback should show after
  a cut changed without the size.
- Layers menu: add a Keep stepper or a Flip side command; five, six or seven
  items with the view slice; whether the slice is a key, a menu item or both.
- View slice: whether the detached camera's position (proposed) or the body's
  feet is the cut in Freelook/FreeCamera, and the exact dominant-axis tie rule.
- Action labels and help for target-based movement (saved action ids stay).
- Default chords while Schematics is off: accept the toast or gate them later.
- Whether the one-shot slice matches the intended workflow (axis at command
  time, adjust key plus wheel, no continuous sync).
- Prototype scope and branch.

## Suggested order

From the 2026-10-20 review, for the maintainer to confirm:

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

An independent read was returned on 2026-10-20. Its gaps are folded into this
note: storage migration, the missing Keep operation, repeat after a target
change, target display without the HUD, and the view slice's coordinate basis.
Its suggested order is recorded above. A prototype may live on a separate
branch; main keeps today's behavior until the design settles.
