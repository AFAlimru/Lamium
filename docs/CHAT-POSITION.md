# Chat position (L-174)

## Current contract (decided 2026-10-14)

Information has a saved, default-off **Chat position** switch and an unbound
toggle key. Enabling it places ordinary HUD chat at the bottom left, with
its bottom 40 GUI units above the screen edge to leave room for the hotbar.
HUD layout provides dragging, keyboard nudging, nine snap positions and
reset; the chat toolbar only offers position controls. Placement follows
the shared edge-anchor rules as the measured message area grows/shrinks.

The game's message contents, font, size, shadow, background, order and fade
are retained. The input/history screen is outside this step. No width,
line-count or appearance settings. The layout editor uses sample text, not
saved chat contents. The feature is Experimental until runtime coverage is
established. BACKLOG owns status; VALIDATION owns runtime evidence.

## Research and first implementation (2026-10-14)

- SDK 26.51.5 exposes `UIControl::mParentRelativePosition`,
  `mCachedPosition`, `mCachedPositionDirty`, `mSize`, `mChildren` and
  `_setCachedPosition()`, and `ScreenView::render(UIRenderContext&)`.
- Mojang's [HUD UI sample](https://github.com/Mojang/bedrock-samples/blob/main/resource_pack/ui/hud_screen.json)
  separates `chat_panel` from `player_position`, the paper-doll padding
  and other children of `chat_stack`. The panel contains a message factory;
  each message has its own text/background and native fade animation.
  This supports a translation-only experiment; it does not establish
  the runtime render/cache order on the installed game.
- `ChatPosition.cpp` gathers at most 512 controls in that panel's subtree,
  resolves dirty cached positions, and validates measured geometry before
  any translation. The relative root and absolute cached positions are
  translated for one render call. A scope guard restores them and marks
  them dirty so vanilla can re-resolve them afterwards. No game pointers
  survive the call; only owned screen/size values inform the editor sample.
- Only the gameplay HUD view is eligible while a player exists and Minecraft
  has foreground focus. Disabled features, menus, world exit and unknown
  game executables retain vanilla placement. Unexpected pack hierarchies,
  clipping and invalid/oversized geometry fail open. The version gate is
  `Chat position (HUD control positions)`.
- Build/tests do not prove that native batching consumes the translated
  caches. First checkpoint: see both text and background move together;
  confirm fading, coordinates remaining in place and disabling restoration.

Build checkpoint: release DLL, LamiumTests and LamiumNativeTests pass; the hook inventory
matches. No runtime result is claimed at this checkpoint.

## Runtime checks (pending)

1. Enable Chat position; send several messages and a wrapped long message.
   Text and backgrounds should appear together at the lower left and fade
   as vanilla does. Coordinates/paper doll must stay where vanilla put them.
2. In HUD layout, drag the chat sample, use arrows and snap positions; close,
   send messages and restart. Placement should persist. Reset restores the
   lower-left default. No font/background/scale controls for chat.
3. Disable; send another message. Open chat input/history and close it, use
   F1, alt-tab, leave/rejoin and change dimensions. The input/history screen
   stays vanilla and no chat offset is left on another screen.
4. Resize/change GUI scale; repeat with Pocket UI and a resource pack. On
   unsupported panel layouts retain vanilla. Verify local world and server
   chat separately. Check the forced `unverified_game` gate separately.
