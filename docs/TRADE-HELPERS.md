# Villager trade helpers

Current work: [BACKLOG L-166](BACKLOG.md#l-166-villager-trade-helpers).
Runtime coverage stays in VALIDATION.md; no L-166 game result yet.

## Contract

The maintainer chose the repeated-trade part on 2026-10-13, through its
first required runtime check. Right-clicking an unlocked trade row should
trade it repeatedly, default on. Left click and Shift retain vanilla
behavior. Favourites and remaining-use presentation are still undecided.

Use the game's own trade requests. Start another round only after the
request succeeds and authoritative inventory and offer state confirm it.
Stop on sold-out offers, missing inputs, insufficient output space, a
rejected/untracked/timed-out request, screen close, focus loss, world or
dimension change, or disabling the feature. Never drop output to continue.
No game pointers are retained across frames or used from window input events.

## Current research (2026-10-13)

The 26.51.5 client SDK exposes:

- `Trade2ScreenController::_onContainerSlotPressed`, `tick`, `onLeave`,
  `TradeItemCollections` (tier and row indexes from a UI property bag),
  and `mVisualOffer` (`MerchantRecipe`, including uses, limits and prices).
- `Trade2ContainerManagerModel::getTradeIndex(tier, index)` and
  `mSelectedTrade`. `Trade2ContainerManagerController` is an empty class
  in the SDK; it exposes no callable trade method.
- `ItemStackRequestPacketPayload` construction from vanilla's request
  batch, craft actions' `mNumCrafts`, and the existing RequestTracker's
  ownership and ItemStackResponse barrier.
- The earlier L-129 packet captures contain `Recipes`, with `netId`,
  `tier`, `uses`, `maxUses`, `buyCountA` and `buyCountB`. Those captures
  establish opening data only, not how uses update after a trade.

Before implementing automatic requests, determine in game:

1. Which button/toggle event and property bag identify a trade row, how
   right click differs from selection, and which vanilla action executes it.
2. Whether a manual trade becomes a tracked item-stack request and whether
   Shift produces one trade or a batch (`crafts` count).
3. Whether the server sends updated `uses` after each trade, or some other
   authoritative offer update is needed. The visual offer may be predicted;
   changes to it alone are not completion evidence.
4. Output routing (inventory or cursor), full-inventory refusal and two-input
   trades, including an enchanted book. These decide the safe request path.

## First runtime checkpoint

Build with `xmake f --repeat_trade_trace=y -y`, then `xmake build Lamium`.
This is an independent, read-only trace; leave the old `trade_trace` off.
It never sends requests, modifies button behavior or automates trades.
The normal build compiles the implementation out.

`L-166` log lines carry a sequence and steady-clock milliseconds:
mouse/modifier input; button and toggle event IDs and property bags;
slot presses; visual offer and small container contents when state changes;
vanilla request batches with IDs/action types/craft counts; applied
ItemStackResponse slots; inventory packets; and received offer uses.
Each stage has its own fixed line budget. Enumerations are bounded and only
owned strings/scalars are remembered. Network observations extend for ten
seconds after closing the trade screen, so a late response remains visible.
Restart the client for a fresh budget when moving from local to BDS checks.
No SNBT files are written and earlier L-129 captures remain intact.

Check locally first, then on BDS 1.26.51.1:

1. Open an unlocked villager offer with enough inputs. Select it with left
   click, trade once through vanilla, wait two seconds, and trade again.
2. Right-click its row once, then try vanilla Shift trading separately.
   Record what each does (selection, one trade, many trades or nothing).
3. Try a locked row, a sold-out offer and a full inventory. Record whether
   output appears on the cursor or in inventory; keep the ordinary flow.
4. Try a two-input enchanted-book offer. Close the screen just after one
   manual trade, wait two seconds, reopen, and record the visible uses.

Expected: vanilla behavior is preserved and the log distinguishes requests
and applied responses from visual updates. No repeated trading is expected
in this checkpoint. Save the log with the build/hash and observed outcomes.
If diagnostics cannot install, preserve the error before proceeding.

Keep the tested DLL/PDB/manifest as a separate local artifact, then reset
`xmake f --repeat_trade_trace=n -y` and rebuild the normal DLL before commit
or release. Deploy a trace artifact only for an explicitly requested test,
with Minecraft closed, and compare source/deployed SHA-256 values.
