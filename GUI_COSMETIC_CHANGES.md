# Device page GUI cosmetic changes

Local modifications to the **Device → Status** screen (camera + Control pane + the
embedded web AMS). All are presentation/UX only — no change to printing, protocol,
or networking. Built and verified on Manjaro/KDE Wayland (DP-3 @ 1.25 scale,
eDP-1 @ 1.0).

## Camera (`src/slic3r/GUI/wxMediaCtrl3.{h,cpp}`)
- **Pan the zoomed live view.** The feed already supported scroll-to-zoom
  (`m_zoom`, 1–5×) but always re-centred. Added `m_pan_x/m_pan_y` + left-drag
  handlers (grab-and-drag), clamped to the frame edges; pan resets when zoom
  returns to 1×.

## Control pane frame (`src/slic3r/GUI/StatusPanel.{hpp,cpp}`)
- **Draggable divider** between the Camera pane (flexes, proportion 1) and the
  Control pane. The fixed middle separator is now a 6 px drag strip
  (`wxCURSOR_SIZEWE`); dragging sets the Control pane width (clamped 360–980 px),
  camera fills the rest. Lambda-bound mouse handlers + `m_pane_divider` /
  `m_divider_*` members.
- **Control column title** ("Control" + the four green buttons): buttons
  auto-size to their text instead of a fixed 128 px each, and the title label
  keeps its natural width (proportion 0), so the bar isn't over-stuffed.
- Control panel given an explicit min width and the web AMS panel set to
  `wxEXPAND` so it fills the column width.

## Web AMS — responsiveness (`.../ams-control-web/AmsControlWebPage.tsx`)
The embedded web AMS was fixed at `CONTENT_WIDTH = 578` and the host locked it to
586 px. Now it tracks the (resizable) Control pane:
- **Scale to fit width**: `fitScale = clamp((viewportWidth − 2·margin) / 578, 0.5, 2)`
  via a `transform: scale()` wrapper; the AMS grows/shrinks with the pane (8 px
  side margin).
- **Content-aware height**: a `ResizeObserver` (re-armed on `display.visible`)
  measures the real content height (`max(scrollHeight, offsetHeight)`), max'd with
  the formula, and the *scaled* height is reported to the host.
- **Host-pixel (DPR) calibration**: on fractional-scale Wayland the host applied
  the reported height in its own pixels while the page measures CSS px, clipping
  the footer. The page now measures `reported ÷ innerHeight` and multiplies its
  report by that ratio — self-correcting (≈1.0 on a 1× screen, ≈1.25 on DP-3).
- **Centring**: the AMS+Ext group (and road-overlay anchors + preview chip) are
  shifted so the pair is centred in `CONTENT_WIDTH` (it had been pulled left by
  the Ext condense).

## Web AMS — layout/appearance
- **Ext box ½ width + condensed** (`AmsControlWebPage.tsx`): the external-spool
  column uses a half-width backplate left-aligned against the main AMS box, so the
  "Ext" box is ~2× narrower and sits snug to the AMS instead of floating right.
- **Footer buttons on their own centred row** (`components/AmsFooter.tsx`,
  `dip.ts`, `geometry.ts`): Auto-refill / Settings / Unload / Load moved below the
  extruder nozzle row (was split left/right on the nozzle row, which clipped
  Load); height formula bumped by that row.
- **Content-hugging box + matching greys** (`AmsControlWebPage.tsx`,
  `ams-control-web.css`): the page background matches the wx pane (`#2d2d31` dark),
  so the full-width webview's side strips vanish; a box wraps just the group.
  AMS box/plate greys unified to `#2d2d31` (were `#1a1a1a` / `#36363c`).
- **Hidden the webview's own scrollbars** on this route only (restored on unmount
  so Filament Manager etc. keep theirs).

## Web AMS — host plumbing
- `src/slic3r/GUI/DeviceWeb/DeviceWebHost.cpp` — `ApplyAmsControlWebHostSize`:
  pin **height only** (let width flex with the pane); height cap raised 360 → 800.
- `src/slic3r/GUI/DeviceTab/AmsControl/wgtAmsControlWebPanel.cpp` — follow the
  reported height only; propagate the new size up **all** ancestors incl.
  `FitInside()` on the scrolled StatusPanel (otherwise the webview was clipped
  until a page switch).

## Build (`.../device_page/vite.config.ts`)
- **Content-hashed asset filenames** (`[name]-[hash].js`) so the WebView can't
  serve a stale cached bundle across rebuilds.

## Build/run notes
- Rebuild web bundle: `ninja -C build device_page_build`; binary:
  `ninja -C build -j$(( $(nproc) - 4 )) bambu-studio`.
- Run via the `~/.local/bin/bambu-studio` wrapper (sets `LD_LIBRARY_PATH` for
  `libglapi.so.0` and `GTK_USE_PORTAL=1`).

## Still open (not done)
- Truncated title strings in the Control title bar ("Contro", "Calibrati…").
