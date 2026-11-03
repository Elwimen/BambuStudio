import { useEffect, useRef, useState, type ReactNode } from 'react';
import { useDeviceWebTheme } from '../../../hooks/useDeviceWebTheme';
import { useSendToCpp } from '../../../hooks/Bridge';
import { useAmsControlWebBridge } from './useAmsControlWebBridge';
import './ams-control-web.css';
import { COLORS, CONTENT_WIDTH, DOWN_ROAD, MIXED_EXT_WIDTH, MIXED_LITE_WIDTH, PREVIEW_BODY_GAP, PREVIEW_STRIP, UNIT_BODY, px } from './dip';
import { extruderIconName, extrudersLeftToRight, unitSlotLayout } from './displayAdapters';
import { SINGLE_EXT_ELBOW, amsControlWebContentHeight, extPanelWidth, isFilamentRoadActive, nozzleIconLefts, nozzleInletX, overlayStageHeight, roadExtruderId, snapRoadX, unitWidth } from './geometry';
import type { PanelView, PreviewItem, SlotLink, SlotView, UnitView } from './types';
import { columnExtSlots, columnPreviewItems, columnShowsPreviewStrip, columnUnits, isLiteMixedBodyRow, isLiteMixedMachine, panelColumns, splitMixedPreview } from './layout';
import { buildRoadOverlay, type RoadBlock, type RoadBlockDraft } from './roadPaths';
import { AmsFooter } from './components/AmsFooter';
import { AmsPreviewBar } from './components/AmsPreviewBar';
import { AmsUnitPanel, type SlotHandlers } from './components/AmsUnitPanel';
import { ExtPanel } from './components/ExtPanel';
import { RoadOverlay } from './components/RoadOverlay';
import { SwitcherBand, SwitcherSetupHint } from './components/SwitcherBand';

const COLUMN_GAP = 10;
const HOST_LAYOUT_MODULE = 'device_host';
const HOST_WIDTH_DIP = 586;

function useReportAmsControlWebHostSize(height: number) {
  const send = useSendToCpp();
  const lastRef = useRef(0);

  useEffect(() => {
    if (height <= 0 || height === lastRef.current) return;
    lastRef.current = height;
    send(
      { version: '1.0', type: 'request', seq: 1_000_000, ts: Date.now() },
      {
        module: HOST_LAYOUT_MODULE,
        submod: 'layout',
        action: 'content_size',
        payload: { width: HOST_WIDTH_DIP, height },
      },
    );
  }, [height, send]);
}

// Mod: the host now sizes the webview to the content, so the page never needs to
// scroll; hide the WebView's own scrollbars on this route (restored on unmount so
// other routes such as Filament Manager keep theirs).
function useHideDocumentScrollbars() {
  useEffect(() => {
    const html = document.documentElement;
    const body = document.body;
    const prev = [html.style.overflow, body.style.overflow];
    html.style.overflow = 'hidden';
    body.style.overflow = 'hidden';
    return () => {
      html.style.overflow = prev[0];
      body.style.overflow = prev[1];
    };
  }, []);
}

// Mod: track the host webview width so the fixed-width AMS layout can be scaled
// to fit the Control pane (which is now resizable via the divider).
function useViewportWidth() {
  const [w, setW] = useState(() => (typeof window === 'undefined' ? HOST_WIDTH_DIP : window.innerWidth));
  useEffect(() => {
    const onResize = () => setW(window.innerWidth);
    window.addEventListener('resize', onResize);
    onResize();
    return () => window.removeEventListener('resize', onResize);
  }, []);
  return w;
}

interface BodyBlock {
  key: string;
  width: number;
  road: RoadBlockDraft;
  render: () => ReactNode;
}

interface ColumnLayout {
  column: PanelView;
  blocks: BodyBlock[];
  previewItems: PreviewItem[];
  innerWidth: number;
  pad: number;
  origin: number;
}

function linkOf(links: SlotLink[], amsId: string, slotId: string) {
  return links.find((link) => link.ams_id === amsId && link.slot_id === slotId);
}

function carryingLink(links: SlotLink[]): SlotLink | undefined {
  return links.find((link) => link.state !== 'idle') ?? links[0];
}

export function AmsControlWebPage() {
  useDeviceWebTheme();
  const {
    state,
    error,
    selectSlot,
    switchAms,
    editSlot,
    readSlot,
    viewSlot,
    openFilamentHint,
    openHumidity,
    openSettings,
    openAutoRefill,
    loadFilament,
    unloadFilament,
    openBridgeDebugDialog,
  } = useAmsControlWebBridge();
  const [debugOpening, setDebugOpening] = useState(false);
  const showDebugButton = Boolean((window as Window & { __internalBuild?: boolean }).__internalBuild);

  const { display } = state;
  const { ams_ext_area: amsExt, ams_preview_area: preview, filament_line_area: lineArea } = display;
  const columns = panelColumns(amsExt);
  const liteStyle = amsExt.lite_style;
  const mixedMachine = isLiteMixedMachine(amsExt.units);
  const bareSingleExt = !liteStyle && state.data.ams_units.length === 0 && amsExt.ext_slots.length === 1;
  const extAmsIds = new Set(amsExt.ext_slots.map((slot) => slot.ams_id));
  const links = lineArea.links;
  const extruders = extrudersLeftToRight(display.extruder_area.extruders);
  const handlers: SlotHandlers = {
    onSelect: (slot: SlotView) => { void selectSlot(slot.ams_id, slot.slot_id); },
    onEdit: (slot: SlotView) => { void editSlot(slot.ams_id, slot.slot_id); },
    onRead: (slot: SlotView) => { void readSlot(slot.ams_id, slot.slot_id); },
    onView: (slot: SlotView) => { void viewSlot(slot.ams_id, slot.slot_id); },
    onFilamentHint: (slot: SlotView) => { void openFilamentHint(slot.ams_id, slot.slot_id); },
  };

  const onPreview = (item: PreviewItem) => {
    void switchAms(item.ams_id);
  };

  const onHumidityClick = (unit: UnitView) => {
    void openHumidity(unit.ams_id);
  };

  const dualNozzle = extruders.length >= 2;
  const hasPreview = preview.visible && (
    mixedMachine
    || columns.some((column) =>
      columnShowsPreviewStrip(column, columnPreviewItems(column, preview.items), dualNozzle),
    )
  );
  useHideDocumentScrollbars();
  const viewportWidth = useViewportWidth();
  const contentHeight = display.visible
    ? amsControlWebContentHeight({
        hasPreview,
        slotAreaVisible: amsExt.visible,
        hasSwitcher: display.switcher_area.visible,
        hasSetupHint: Boolean(display.switcher_area.show_setup_hint && display.switcher_area.setup_hint),
      })
    : 0;
  // Only scale DOWN to fit a narrow pane; never up — so at wide panes the box stays
  // native size and centres like the upper controls, and the height never scales
  // past the pane (which was clipping the footer buttons).
  // Mod: fill the pane width minus a small side margin (scales up as well as down).
  // Safe now that the reported height follows the scaled content.
  const AMS_SIDE_MARGIN = 8;
  const fitScale = Math.min(2, Math.max(0.5, (viewportWidth - 2 * AMS_SIDE_MARGIN) / CONTENT_WIDTH));
  // Measure the real content height (the formula can under-count the footer) so the
  // host panel is tall enough to show the button row without an internal scroll.
  const contentRef = useRef<HTMLDivElement>(null);
  const [measuredHeight, setMeasuredHeight] = useState(0);
  useEffect(() => {
    const el = contentRef.current;
    if (!el) return;
    // scrollHeight captures descendants that overflow the flow box (the extruder
    // nozzle is absolutely positioned and taller than its row), which offsetHeight
    // misses — that was clipping the footer button row.
    const update = () => setMeasuredHeight(Math.max(el.scrollHeight, el.offsetHeight));
    const ro = new ResizeObserver(update);
    ro.observe(el);
    update();
    return () => ro.disconnect();
    // Re-run when the panel becomes visible: on first mount the content div isn't
    // rendered yet (display.visible false), so the observer must attach once it is.
  }, [display.visible]);
  // +50px breathing room on the AMS box (requested).
  const AMS_BOX_EXTRA_H = 8;   // was 50; trimmed so the graphics can grow instead
  const layoutHeight = Math.max(measuredHeight, contentHeight) + AMS_BOX_EXTRA_H;

  // Mod: host-pixel calibration. On fractional-scale Wayland (e.g. 1.25x) the host
  // applies the reported height in its own pixels while the page measures CSS px,
  // so the viewport came out reported/1.25 (379 -> 303) and clipped the buttons.
  // Measure the real ratio (what we reported vs. the viewport we got) and scale the
  // report by it. Self-correcting on any monitor; 1.0 where units already match.
  const reportedRef = useRef(0);
  const [hostScale, setHostScale] = useState(1);
  useEffect(() => {
    const onResize = () => {
      const r = reportedRef.current;
      const ih = window.innerHeight;
      if (r > 0 && ih > 0) {
        const k = r / ih;
        if (k > 1.02 && k < 3) setHostScale((prev) => (Math.abs(prev - k) > 0.01 ? k : prev));
      }
    };
    window.addEventListener('resize', onResize);
    return () => window.removeEventListener('resize', onResize);
  }, []);
  const reportHeight = Math.round(layoutHeight * fitScale * hostScale);
  reportedRef.current = reportHeight;
  useReportAmsControlWebHostSize(reportHeight);

  if (!display.visible) {
    return <div data-testid="ams-control-web-page" className="hidden" />;
  }

  const columnBlocks = columns.map((column) => {
    const blocks: BodyBlock[] = [];
    const unitsInColumn = columnUnits(column, amsExt.units);
    const mixedBody = isLiteMixedBodyRow(unitsInColumn);

    for (const unit of unitsInColumn) {
      const slotLayout = unitSlotLayout(unit);
      const mixedCompact = mixedBody && slotLayout === 'lite_cross';
      const blockWidth = unitWidth(slotLayout, mixedCompact);
      const unitLinks = unit.slots
        .map((slot) => linkOf(links, slot.ams_id, slot.slot_id))
        .filter((link): link is SlotLink => !!link);
      const carrying = carryingLink(unitLinks);
      const destId = carrying ? roadExtruderId(carrying) : undefined;
      const drawn = destId !== undefined;
      const passing = !!carrying && destId !== undefined && isFilamentRoadActive(carrying);
      blocks.push({
        key: `unit-${column.pos}-${unit.ams_id}`,
        width: blockWidth,
        road: {
          key: `unit-${column.pos}-${unit.ams_id}`,
          kind: slotLayout,
          slots: unit.slots,
          hub: unit.hub,
          links: unitLinks,
          bodyWidth: slotLayout === 'lite_cross' ? blockWidth : undefined,
          exit: drawn
            ? {
                extruderId: destId,
                active: passing,
                color: passing ? carrying.color : '',
              }
            : undefined,
        },
        render: () => (
          <AmsUnitPanel
            unit={unit}
            slotLayout={slotLayout}
            bodyWidth={blockWidth}
            onHumidityClick={onHumidityClick}
            {...handlers}
          />
        ),
      });
    }

    for (const slot of columnExtSlots(column, amsExt.ext_slots)) {
      const link = linkOf(links, slot.ams_id, slot.slot_id);
      const destId = link ? roadExtruderId(link) : undefined;
      const drawn = destId !== undefined;
      const framed = bareSingleExt;
      const extShift = framed ? SINGLE_EXT_ELBOW : 0;
      const panelWidth = extPanelWidth(mixedBody && !framed);
      const passing = !!link && destId !== undefined && isFilamentRoadActive(link);
      blocks.push({
        key: `ext-${slot.ams_id}-${slot.slot_id}`,
        width: framed ? UNIT_BODY.width : panelWidth,
        road: {
          key: `ext-${slot.ams_id}-${slot.slot_id}`,
          kind: 'ext',
          liteStyle,
          framed,
          slotOffset: extShift,
          panelWidth,
          link,
          exit: drawn
            ? {
                extruderId: destId,
                active: passing,
                color: passing ? link.color : '',
              }
            : undefined,
        },
        render: () => (
          <ExtPanel
            slot={slot}
            liteStyle={liteStyle}
            framed={framed}
            slotOffset={extShift}
            panelWidth={panelWidth}
            {...handlers}
          />
        ),
      });
    }

    return { column, blocks, previewItems: columnPreviewItems(column, preview.items) };
  });

  const splitColumns = columnBlocks.length > 1;
  const columnWidth = splitColumns ? CONTENT_WIDTH / columnBlocks.length : CONTENT_WIDTH;

  const laidOutBase: ColumnLayout[] = columnBlocks.map((entry, index) => {
    const innerWidth =
      entry.blocks.reduce((sum, block) => sum + block.width, 0)
      + Math.max(entry.blocks.length - 1, 0) * COLUMN_GAP;
    const isExtColumn =
      entry.blocks.length > 0 && entry.blocks.every((block) => block.key.startsWith('ext-'));
    // wxALIGN_CENTER integer-divides leftover space. A 264px AMS in a 289px
    // half-row is 12.5px; floor it so overlay x stays on whole pixels.
    // Mod: the Ext column uses a half-width backplate (see the body render). Left-
    // align it to the column's inner edge instead of centring the 78px card in the
    // 289px half-row, so the gap to the AMS box is condensed. The box, card and
    // road all follow because each is anchored at columnWidth*index + pad.
    const extPlateWidth = Math.round((columnWidth - COLUMN_GAP) / 2);
    const pad = isExtColumn
      ? Math.max(0, Math.round((extPlateWidth - innerWidth) / 2))
      : Math.floor((columnWidth - innerWidth) / 2);
    const origin = snapRoadX(columnWidth * index + pad);
    return { ...entry, innerWidth, pad, origin };
  });

  // Mod: centre the AMS+Ext group as a whole. After the Ext plate was halved and
  // pulled left, the pair sat left of centre with dead space on the right. Measure
  // the visible plate extents of all columns and shift the whole row (and the road
  // overlay origins with it) so the group is centred in CONTENT_WIDTH.
  let groupShift = 0;
  let groupWidth = CONTENT_WIDTH;   // visible width of the AMS+Ext group (content px)
  if (splitColumns && laidOutBase.length > 0) {
    let left = Infinity;
    let right = -Infinity;
    laidOutBase.forEach((entry, index) => {
      const isExt = entry.blocks.length > 0 && entry.blocks.every((block) => block.key.startsWith('ext-'));
      const colLeft = columnWidth * index;
      if (isExt) {
        const plateW = Math.round((columnWidth - COLUMN_GAP) / 2);
        const l = colLeft + entry.pad + entry.innerWidth / 2 - plateW / 2;
        left = Math.min(left, l);
        right = Math.max(right, l + plateW);
      } else {
        left = Math.min(left, colLeft + COLUMN_GAP / 2);
        right = Math.max(right, colLeft + columnWidth - COLUMN_GAP / 2);
      }
    });
    if (Number.isFinite(left) && Number.isFinite(right)) {
      groupShift = Math.round(CONTENT_WIDTH / 2 - (left + right) / 2);
      groupWidth = Math.round(right - left);
    }
  }
  const laidOut: ColumnLayout[] = laidOutBase.map((entry) => ({
    ...entry,
    origin: snapRoadX(entry.origin + groupShift),
  }));

  const nozzleXOf = (extruderId: number) => {
    const names = extruders.map((extruder) => extruderIconName(display.extruder_area.extruders, extruder));
    const lefts = nozzleIconLefts(names);
    const index = extruders.findIndex((extruder) => extruder.id === extruderId);
    if (index < 0) return snapRoadX(CONTENT_WIDTH / 2);
    return nozzleInletX(names[index], lefts[index]);
  };

  const overlayBlocks: RoadBlock[] = [];
  laidOut.forEach((entry) => {
    let cursor = entry.origin;
    for (const block of entry.blocks) {
      overlayBlocks.push({ ...block.road, origin: cursor } as RoadBlock);
      cursor += block.width + COLUMN_GAP;
    }
  });
  const overlay = buildRoadOverlay({
    blocks: overlayBlocks,
    slotAreaVisible: amsExt.visible,
    nozzleXOf,
  });

  const previewEntries = laidOut.filter(
    (entry) =>
      preview.visible &&
      columnShowsPreviewStrip(entry.column, entry.previewItems, extruders.length >= 2),
  );
  const mixedPreview = mixedMachine ? splitMixedPreview(preview.items, extAmsIds) : null;

  return (
    <div
      data-testid="ams-control-web-page"
      className="ams-control-web-page relative flex w-full flex-col items-center py-[4px]"
      style={{ background: 'var(--ams-web-host-pane-bg)' }}
    >
      <div style={{ width: px(CONTENT_WIDTH * fitScale), height: px(layoutHeight * fitScale) }}>
      <div
        ref={contentRef}
        className="relative flex flex-col"
        style={{ width: px(CONTENT_WIDTH), transform: `scale(${fitScale})`, transformOrigin: 'top left', isolation: 'isolate' }}
      >
        {/* Mod: content-hugging box behind the AMS+Ext group (no side padding). */}
        <div
          aria-hidden
          style={{
            position: 'absolute',
            zIndex: -1,
            top: 0,
            left: px(Math.round((CONTENT_WIDTH - groupWidth) / 2)),
            width: px(groupWidth),
            height: px(layoutHeight),
            background: 'var(--ams-web-host-box-bg)',
            borderRadius: px(4),
          }}
        />
        {preview.visible && mixedPreview ? (
          <>
            <div
              className="flex justify-center"
              style={{ width: px(CONTENT_WIDTH), gap: px(COLUMN_GAP) }}
            >
              <AmsPreviewBar items={mixedPreview.ams} extAmsIds={extAmsIds} onSelect={onPreview} width={PREVIEW_STRIP.width} />
              <AmsPreviewBar items={mixedPreview.lite} extAmsIds={extAmsIds} onSelect={onPreview} width={MIXED_LITE_WIDTH} />
              <AmsPreviewBar items={mixedPreview.ext} extAmsIds={extAmsIds} onSelect={onPreview} width={MIXED_EXT_WIDTH} />
            </div>
            <div style={{ height: px(PREVIEW_BODY_GAP) }} />
          </>
        ) : previewEntries.length > 0 ? (
          <>
            <div
              className="flex"
              style={
                splitColumns
                  ? { width: px(CONTENT_WIDTH), transform: `translateX(${groupShift}px)` }
                  : { justifyContent: 'center', gap: px(COLUMN_GAP), width: px(CONTENT_WIDTH) }
              }
            >
              {(splitColumns ? laidOut : previewEntries).map((entry) => {
                const showStrip =
                  preview.visible &&
                  columnShowsPreviewStrip(entry.column, entry.previewItems, extruders.length >= 2);
                const bar = showStrip ? (
                  <AmsPreviewBar
                    items={entry.previewItems}
                    extAmsIds={extAmsIds}
                    onSelect={onPreview}
                  />
                ) : null;
                if (!splitColumns) {
                  return bar ? <div key={entry.column.pos}>{bar}</div> : null;
                }
                return (
                  <div
                    key={entry.column.pos}
                    className="flex justify-center"
                    style={{ width: px(columnWidth) }}
                  >
                    {bar}
                  </div>
                );
              })}
            </div>
            <div style={{ height: px(PREVIEW_BODY_GAP) }} />
          </>
        ) : null}

        <div className="relative overflow-visible" style={{ width: px(CONTENT_WIDTH), minHeight: px(overlayStageHeight(amsExt.visible)) }}>
          <div
            className="flex"
            style={splitColumns ? { gap: 0, marginLeft: px(groupShift) } : { marginLeft: px(laidOut[0]?.origin ?? 0), gap: px(COLUMN_GAP) }}
          >
            {laidOut.map((entry) => {
              // Mod: narrow an Ext-only column's backplate to half width and
              // centre it on the ext card, so the "Ext" box is ~2x narrower
              // instead of matching the full AMS-column width. The ext card stays
              // at x = entry.pad (where the road overlay, anchored at
              // entry.origin + pad, expects it), so the filament road stays aligned.
              const isExtColumn =
                entry.blocks.length > 0 && entry.blocks.every((block) => block.key.startsWith('ext-'));
              const fullPlate = columnWidth - COLUMN_GAP;
              const plateWidth = isExtColumn ? Math.round(fullPlate / 2) : fullPlate;
              const plateMarginLeft = isExtColumn
                ? Math.max(0, Math.round(entry.pad + entry.innerWidth / 2 - plateWidth / 2))
                : COLUMN_GAP / 2;
              const platePaddingLeft = isExtColumn
                ? Math.max(0, Math.round((plateWidth - entry.innerWidth) / 2))
                : Math.max(0, entry.pad - COLUMN_GAP / 2);
              return (
              <div
                key={entry.column.pos}
                className="flex flex-col"
                style={splitColumns ? { width: px(columnWidth) } : undefined}
              >
                {amsExt.visible ? (
                  // One backplate per group: `entry.blocks` only ever holds the
                  // cell this column has open, so the gap between two
                  // single-slot units is painted as part of the same plate.
                  // left_right gives both columns the same full-width plate so
                  // the two sides stay symmetric whatever they hold; the
                  // centering pad moves inside it, leaving the blocks (and the
                  // road origin computed from that pad) where they were. A
                  // single column has no opposite side to match, so there the
                  // plate shrinks to the blocks.
                  <div
                    className={splitColumns ? 'flex' : 'flex self-start'}
                    style={{
                      gap: px(COLUMN_GAP),
                      background: COLORS.unitBg,
                      ...(splitColumns
                        ? {
                            width: px(plateWidth),
                            marginLeft: px(plateMarginLeft),
                            // Half the seam comes off each side, so the pad has
                            // to shed the same amount or the blocks would slide
                            // right and leave the road origin behind. (For the Ext
                            // column these are recomputed above for a half-width plate.)
                            paddingLeft: px(platePaddingLeft),
                          }
                        : null),
                    }}
                  >
                    {entry.blocks.map((block) => (
                      <div key={block.key} className="shrink-0" style={{ width: px(block.width), minWidth: px(block.width), maxWidth: px(block.width) }}>{block.render()}</div>
                    ))}
                  </div>
                ) : null}
              </div>
              );
            })}
          </div>
          <div style={{ height: px(DOWN_ROAD.height) }} />
          <RoadOverlay
            model={overlay}
            showSlot={amsExt.visible}
            showLine={lineArea.visible}
          />
        </div>

        <SwitcherBand switcher={display.switcher_area} />

        <AmsFooter
          actions={state.actions}
          extruderArea={display.extruder_area}
          onSettings={() => { void openSettings(); }}
          onAutoRefill={() => { void openAutoRefill(); }}
          onLoad={() => { void loadFilament(state.selected_ams_id, state.selected_slot_id); }}
          onUnload={() => { void unloadFilament(state.selected_ams_id, state.selected_slot_id); }}
          showDebug={showDebugButton}
          debugOpening={debugOpening}
          onDebug={async () => {
            setDebugOpening(true);
            try {
              await openBridgeDebugDialog(state);
            } finally {
              setDebugOpening(false);
            }
          }}
        />

        <SwitcherSetupHint switcher={display.switcher_area} />

      </div>
      </div>

      {error ? <div className="mt-1 text-xs text-red-600">{error}</div> : null}
    </div>
  );
}
