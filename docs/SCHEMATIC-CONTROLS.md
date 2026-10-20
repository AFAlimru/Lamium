# Schematic controls redesign

Status: design discussion, 2026-10-20; three independent reviews and all
recommendations adopted by the maintainer the same day. **Nothing
implemented.** This note records the agreed direction; only the items under
"Open questions" remain undecided.

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
- The move-to-feet key acts on the current target like the menu's item: the
  placement origin, corner 2, or the area (corner 1 to the feet and corner 2
  follows). With no target it toasts. The help says "Move the target to your
  feet"; the action id stays.
- Display: the target name in the schematic HUD when it is on, the target's
  frame or mark emphasized in the world, and the target named in move toasts.
  The HUD is optional, so the HUD-off case relies on the frame/mark and the
  toasts. Where the placement and area frames overlap, the active frame is
  drawn last with a brighter or thicker stroke.
- Cycle only valid targets: the placement when one is selected, a corner when
  it is set, the area when both corners are set. An explicit **no target**
  state exists when nothing is valid.
- After a delete, area clear, visibility change or placement selection,
  revalidate the target and take the first valid one in this order: placement
  (when selected), corner 1, corner 2, area, else no target.
- With no target, the direction keys toast that there is nothing to move and
  the HUD shows `Move target: none`; the target key cycles to the next valid
  target and names it.
- Reset to the placement on world/dimension change and when a placement is
  selected or placed.
- The looked-at selection extends "select the placement you look at" with the
  ray and distance rules below.

### Repeat the last adjustment

- Repeats movement, rotate/mirror and the layer cut only. Excludes the layer
  axis/mode, placement selection and the move target.
- Key actions update the remembered adjustment, not only menu steppers, so
  the adjust key is consistent across both surfaces.
- The memory stores the **kind** of adjustment, not its last sign; the wheel
  direction chooses the sign. After a back step, adjust + wheel up moves
  forward. The toast names the value.
- Clear the memory on target change, another placement selection,
  world/dimension change, feature disable and session reset, so a rotate done
  on the placement cannot repeat after the context changed. A failed operation
  does not update it. The current code does not clear on stop; the
  implementation adds it.
- A successful view slice sets the memory to the layer cut, so the next adjust
  plus wheel moves the cut.
- Rotate, mirror and layer operations still act on the selected placement; the
  repeat toast names the operation and what it acts on.

### Corners and default keys

- Corner 1 and corner 2 keep separate actions. A setter is often re-run after
  the view moves, so merging them would hurt.
- Default binding only for forward/back: Alt + Wheel up/down. No corner
  defaults and no edit mode in this round.
- While Schematics is off, the default chord is still consumed and shows the
  turn-it-on toast. Accepted for this round; revisit when Schematics leaves
  Experimental or if it annoys non-users (gating the input on the feature
  switch is the alternative).

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
- The Layers menu is **Axis / Cut / Mode / Keep / Slice from view / Cut here**.
  Show all is dropped because Mode already has All. Keep is a stepper (low /
  high); in only/all it has no visible effect but stays editable for "up to".
  The slice is also available as an unbound key.
- Cut here keeps the axis and keep, sets the cut to the view source's layer
  along that axis (feet in normal play, detached camera in FreeLook/
  FreeCamera), and switches All to Only so the change shows.
- Changing the axis sets the cut to the view source's layer along the new
  axis, clamped and toasted; the numeric offset is not carried across axes.
- **View slice** is one-shot. The axis is the dominant axis of the full view
  vector (largest |x|, |y| or |z|), not the 60-degree movement rule: looking 30
  degrees down at a wall still slices on X/Z, while looking 70 degrees down
  slices on Y. Ties use the order Y, X, Z.
- The slice's cut is the view source's layer along that axis: the player's feet
  in normal play and the detached camera's block in Freelook or FreeCamera (the
  same view source as the movement direction). It is clamped to the placement
  and a toast names the world coordinate. The crosshair is not used: it can
  point at a ghost, inside a placement or outside the build, and building from
  the bottom needs a stable reference.
- The slice sets mode to only and keep to the side away from the camera. The
  axis stays fixed until the command runs again; adjust plus wheel moves the
  cut. No continuous view sync, which would restart the check on every look.
- Examples: standing at Y=64 looking east slices on X at the player's X;
  looking 70 degrees down slices on Y at Y=64; FreeCamera far away looking
  down slices on Y at the camera's Y, not the body's; a 90-degree rotation
  swaps the X/Z extent and clamps the local cut.
- Check and Materials read the same cut region; a change restarts the check
  once.

### Storage and migration

- Keep the document at `version: 1`. Add a separate per-placement object,
  `layerView: { axis: "y"|"x"|"z", cut: <offset>, keep: "low"|"high",
  mode: "all"|"only"|"upTo" }`. The old six-direction `layerAxis` key cannot be
  reused: an older Lamium would read a different direction.
- Leave the legacy `layerAxis`/`layerMode`/`layer` fields for older readers.
  Older Lamium ignores `layerView`; the new reader prefers it.
- When the placed size is known, **always** dual-write the exact legacy
  fields: all is size-independent; only or up-to low uses the low direction
  with `index = cut`; up-to high uses the high direction with
  `index = extent - 1 - cut`. An older Lamium then shows the same cells.
- Never convert without the size: a placement whose structure is missing keeps
  its legacy fields and is written back unchanged; other placements can still
  be saved. `layerView` is added when the structure loads and the placement is
  next saved.
- Cut edits are allowed only while the placed size is known, so the legacy
  fields are never stale. Without the size the screen says the layer view
  cannot be edited yet.
- A mixed file round-trips both forms. Rollback to an older Lamium reads the
  last legacy fields and shows the same view.

### Looked-at selection

- Use the same drawn view ray as movement (`detachedViewRay` when a detached
  view owns the view, otherwise the player's eye and view), not the body view
  in FreeCamera.
- A corner the crosshair hits directly wins before any distance comparison,
  including when the camera is inside the placement box. If both corners share
  the cell, keep the current target when it is one of them, else prefer corner
  1.
- Otherwise compare distances first: a placement at its box entry along the
  ray, and the area only when the crosshair block is inside it, at the hit
  distance (the box entry is not used, or the area would win over a placement
  inside it).
- On equal distance, prefer placement, then area. A corner inside a placement
  that is not hit directly is reached through the target key, not by looking
  through the box.
- Looking at air inside the area selects nothing.

## Third review (2026-10-20, adopted)

The four points were adopted the same day and are folded into the sections
above; nothing remains proposed.

## Open questions

- Action labels and help for target-based movement (saved action ids stay).
- Prototype scope and branch.

## Suggested order

From the 2026-10-20 reviews, for the maintainer to confirm:

1. **L-231 first**: move target, target display and repeat cleanup with the
   current layer model, so the operation feel can be checked before the layer
   model changes.
2. **L-234 as a separate step**: prove with pure logic that the conversion from
   every old direction and mode shows the same cells, then implement the cut
   plane and the storage form.
3. **Then inspection work**: L-183's single-row ignore first; L-190's
   multi-selection after it, sharing only the parts that fit. L-185 stays a
   separate, larger task.

## Not in this round

- Item-based operation and a persistent edit mode.
- The Files/Check 3D preview's own layer peel.
- Other check or material semantics.

## Files likely touched

`Placement.h`, `PlacementStore.cpp`, `GhostBlocks.cpp`, the verification and
materials paths, `MenuModel.h`, `SchematicActions.cpp`, `SchematicsView.cpp`,
the three-language help, and `tests/SchematicTests.cpp`.

## Review

Three independent reads were returned on 2026-10-20. The second found the
`layerAxis` key collision in the first migration proposal, the missing repeat
setup after a slice, target invalidation after delete/clear, and the
distance/view-source gap in the looked-at selection. The third found the
corner-inside-the-box selection gap, the missing move-to-feet unification, the
undefined Cut-here/axis handover, and the incomplete repeat clearing. All are
adopted above. A prototype may live on a separate branch; main keeps today's
behavior until the design settles.
