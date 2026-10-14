# Item Transfer

## Current status

L-169's 0.2.2 contract is in BACKLOG.md and DESIGN.md. Its new gestures,
switches and additional screen support are not implemented yet. The
contract requires a native-handler investigation before implementation.
The existing four gestures are unchanged.

## Research checkpoint (2026-10-15)

Maintainer observation, vanilla storage screen:

- Alt + left click selects an item like an ordinary click. The modified
  mouse operation reaches the game; the loader's Alt key notification is
  not yet observed.
- Shift + right drag transfers the whole hovered stack. Its L-169
  replacement deliberately leaves one item; switching that gesture off
  must preserve vanilla's whole-stack transfer.
- Follow-up: the chest's Alt + left click, Shift + right drag and ordinary
  take/place operations behave as before. Vanilla Shift + left click routes
  items to the expected roles in a furnace, brewing stand and beacon.
  The maintainer reports that a furnace's fuel slot also accepts an item
  that cannot burn; exact items and counts were not supplied. Brewing and
  beacon acceptance appeared restricted to suitable items, not exhaustively
  checked. Follow native routing and acceptance; occupying a fuel slot is
  not evidence that the item is fuel.
- The installed DLL found at follow-up differs from the prepared diagnostic
  DLL, and the current log has no `L-169` lines. These observations do not
  establish native handler names, collection names or auto-place orders.

Source evidence:

- `TransferSession.cpp` already invokes `_handleAutoPlace(amount,
  collection, index)` on the client/UI thread, one request at a time,
  through `RequestTracker`. Existing runtime coverage is in VALIDATION.md.
- The 26.51.5 SDK exposes `_handleTakeAll(collection, index)` and
  `_handlePlaceAll(collection, index)`, and manager `handlePlaceAll` takes
  `SelectedSlotInfo` plus a destination. Their names do not establish
  that they transfer a side or matching stacks. Do not call them as bulk
  operations without observing their cursor and slot effects.
- Native `tryGetAutoPlaceOrder(collection)` returns per-source target
  collections with `stopWhenPlaced`. This is the candidate route for
  role-specific destinations. The reported vanilla routing above does not
  yet identify the registered furnace/brewing/beacon collections and orders.
- Today's transfer is restricted by both `ScreenTracker`'s
  `_isContainerScreen()` check and `TransferSession`'s ordinary-storage
  type/collection whitelist. The claim that the first check excludes the
  missing screens has not been established; observe both before changing
  tracking shared with sorting and trading.
- Furnace and brewing controllers override hover callbacks in the SDK.
  The existing hover tracker observes only the base and crafting callbacks;
  check whether the overrides delegate before adding coverage.

`--transfer_trace=y` now adds bounded, read-only diagnostics. It observes
focused screen names and `_isContainerScreen`, collection sizes and items,
native auto-place order, Alt key notifications and OS modifier state,
and ordinary take-all/place-all/auto-place calls. No diagnostic invokes
these operations, changes input cancellation, or retains a game pointer.
The native observations are game-version gated. A handler's
`predicted-after` snapshot is local prediction, not server confirmation.
The budgets cover at most 32 collections, 256 slots per snapshot and
32 targets per route; restart Minecraft if the sample budget is exhausted.

## First runtime checkpoint

Use a diagnostic build without a release tag. Disable Item Transfer in
Lamium before observing vanilla; otherwise its existing gestures can
consume the input. Record build commit, DLL SHA-256, game/loader versions,
local or server, resource packs, and the actual final slot/cursor counts.
The log alone does not confirm authoritative completion.

1. Open a chest with two matching stacks and one different stack on each
   side. Try Alt + left click, then cancel/return the cursor item. Try
   Shift + right drag, ordinary take/place clicks, Shift + left click and
   the controller's take-all/place-all controls if available. Record which
   slots change, whether the cursor gets an item, and which native handler
   runs. An unavailable controller control is an unchecked case.
2. Open chest, barrel, shulker box, ender chest, dispenser, dropper, hopper,
   furnace, blast furnace, smoker, brewing stand, crafter and beacon once.
   Check the `L-169 focused view`, `layout` and `route` lines. Record missing
   layouts, collections and callbacks rather than guessing their names.
3. With vanilla Shift + left click, send fuel, a smeltable item and an
   invalid item to each furnace; bottles, an ingredient and blaze powder
   to the brewing stand; payment and an invalid item to the beacon. Check
   exact final counts and destinations, including full targets. Observe
   taking outputs and moving ordinary storage stacks back to inventory.
4. Repeat on a server. Keep the local and server evidence separate.

After this checkpoint, report the handler effects and selected routing
path before implementing L-169's new operations. Then test every new
gesture and child switch, matching versus differently named/enchanted
stacks, hotbar preservation for everything-from-player, all-but-one with
counts 1/2/full, full or invalid destinations, and interruption by closing,
focus loss, cursor item, text editing, rejection, timeout and changed source
counts. Record checked screens in DESIGN and runtime evidence in
VALIDATION-LOG/VALIDATION; build/tests alone do not close L-169.
