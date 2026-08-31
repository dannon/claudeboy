import Toybox.Lang;
import Toybox.WatchUi;
import Toybox.Graphics;
import Toybox.System;
import Toybox.Application;
import Toybox.Application.Storage;
import Toybox.Time;

// The Instinct 2X has a round subscreen let into the top-right of the display,
// and an opaque bezel ring around it: pixels inside the circle show, pixels
// under the ring do not, and the main window's own corners are cut ~40px deep.
// So the page is laid out around that circle instead of as a plain stack: the
// worst window's remaining budget goes INSIDE it as a ring gauge, its name and
// pace sit to the left, and the other windows take the full-width band below.
class ClaudeBoyView extends WatchUi.View {

    var mIndex as Number = 0;          // which provider is showing
    var mSnap as Model.Snapshot or Null = null;
    var mFetching as Boolean = false;
    var mNote as String = "";

    function initialize() { View.initialize(); }

    function onShow() as Void {
        mSnap = Cache.load();
        refresh();
        Demo.start(self);
    }

    function refresh() as Void {
        if (mFetching) { return; }
        mFetching = true;
        mNote = "SYNC";
        Fetch.start(method(:onFetched));
        WatchUi.requestUpdate();
    }

    function onFetched(code as Number, data as Dictionary or Null) as Void {
        mFetching = false;
        if (code == 200 && data != null) {
            var s = Model.fromDict(data);
            if (s != null) {
                mSnap = s;
                Storage.setValue("snap", data);
                Storage.setValue("snapAt", Time.now().value());
                mNote = "";
            } else {
                mNote = "BAD DATA";
            }
        } else if (code == -9999) {
            mNote = "SET TOKEN";        // app settings not filled in yet
        } else if (Fetch.isExpectedOffline(code)) {
            mNote = "NO PHONE";         // normal and frequent, not an error
        } else {
            mNote = "ERR " + code.toString();
        }
        WatchUi.requestUpdate();
    }

    function nextProvider() as Void {
        if (mSnap == null || mSnap.providers.size() == 0) { return; }
        mIndex = (mIndex + 1) % mSnap.providers.size();
        WatchUi.requestUpdate();
    }

    function onUpdate(dc as Graphics.Dc) as Void {
        dc.setColor(Graphics.COLOR_BLACK, Graphics.COLOR_BLACK);
        dc.clear();
        dc.setColor(Graphics.COLOR_WHITE, Graphics.COLOR_TRANSPARENT);

        var w = dc.getWidth();
        var h = dc.getHeight();

        if (mSnap == null || mSnap.providers.size() == 0) {
            dc.drawText(w / 2, h / 2, Graphics.FONT_SMALL,
                        mNote.length() > 0 ? mNote : "NO SIGNAL",
                        Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER);
            return;
        }

        if (mIndex >= mSnap.providers.size()) { mIndex = 0; }
        var p = mSnap.providers[mIndex];
        var now = mSnap.nowSec();

        var sub = (WatchUi has :getSubscreen) ? WatchUi.getSubscreen() : null;
        var rows = p.lines;
        var top = 34;
        if (sub != null && p.lines.size() > 0) {
            var hi = worstIndex(p);
            drawHeadline(dc, p, p.lines[hi], now, sub);
            drawDots(dc, sub, mSnap.providers.size());
            rows = [] as Array<Model.Line>;
            for (var i = 0; i < p.lines.size(); i++) {
                if (i != hi) { rows.add(p.lines[i]); }
            }
            // Just under the headline's third text line: the box ends at 62,
            // that line is centred at 70 and its glyphs reach ~78.
            top = subY(sub) + sub.height + 20;
        } else {
            var head = p.displayName.toUpper();
            if (mSnap.providers.size() > 1) {
                head += "  " + (mIndex + 1).toString() + "/" + mSnap.providers.size().toString();
            }
            dc.drawText(w / 2, 18, Graphics.FONT_XTINY, head,
                        Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER);
        }

        drawRows(dc, rows, now, top, h - 26);

        // Footer: staleness wins over any transient note, because old numbers
        // shown as current is the one thing this display must never do.
        var foot = Model.stalenessText(mSnap);
        if (foot.length() == 0) { foot = mNote; }
        if (foot.length() > 0) {
            dc.drawText(w / 2, h - 16, Graphics.FONT_XTINY, foot,
                        Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER);
        }
    }

    // The window closest to running out leads, same rule as the glance.
    function worstIndex(p as Model.Provider) as Number {
        var best = 0;
        var bestPct = 101;
        for (var i = 0; i < p.lines.size(); i++) {
            var pct = p.lines[i].remainingPct();
            if (pct < bestPct) { bestPct = pct; best = i; }
        }
        return best;
    }

    function subX(sub as Graphics.BoundingBox) as Number {
        return (sub.x == null) ? 0 : (sub.x as Number);
    }

    function subY(sub as Graphics.BoundingBox) as Number {
        return (sub.y == null) ? 0 : (sub.y as Number);
    }

    // Ring gauge in the subscreen, remaining budget clockwise from twelve,
    // and the provider / window / pace stacked to its left where the main
    // window is still visible beside the bezel.
    function drawHeadline(dc as Graphics.Dc, p as Model.Provider, line as Model.Line,
                          now as Number, sub as Graphics.BoundingBox) as Void {
        var sx = subX(sub);
        var sy = subY(sub);
        var cx = sx + sub.width / 2;
        var cy = sy + sub.height / 2;
        var r = sub.width / 2 - 6;
        var pct = line.remainingPct();
        var pace = line.pace(now);

        if (pace == Model.PACE_BURNOUT) {
            // Burning through the window: the whole disc lights up. On a 1-bit
            // panel that is the loudest thing this corner can do.
            dc.fillCircle(cx, cy, r + 3);
            dc.setColor(Graphics.COLOR_BLACK, Graphics.COLOR_TRANSPARENT);
        } else {
            dc.setPenWidth(1);
            dc.drawCircle(cx, cy, r);
            if (pct >= 100) {
                dc.setPenWidth(5);
                dc.drawCircle(cx, cy, r);
            } else if (pct > 0) {
                dc.setPenWidth(5);
                dc.drawArc(cx, cy, r, Graphics.ARC_CLOCKWISE, 90, (450 - (pct * 360 / 100)) % 360);
            }
            dc.setPenWidth(1);
        }
        dc.drawText(cx, cy, pct >= 100 ? Graphics.FONT_XTINY : Graphics.FONT_SMALL,
                    pct.toString() + "%",
                    Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER);
        dc.setColor(Graphics.COLOR_WHITE, Graphics.COLOR_TRANSPARENT);

        // Left of the ring the visible width is about 90px, and the chamfer
        // eats the top-left, so the first line starts level with the ring's
        // centre rather than at the top. Glyphs are ~15px tall in this font;
        // 18px is the tightest pitch that still reads as separate lines.
        var lx = 14;
        var maxW = sx - 12 - lx;
        var ty = cy + 3;
        dc.drawText(lx, ty, Graphics.FONT_XTINY,
                    Fit.toWidth(dc, p.displayName, "", Graphics.FONT_XTINY, maxW),
                    Graphics.TEXT_JUSTIFY_LEFT | Graphics.TEXT_JUSTIFY_VCENTER);
        dc.drawText(lx, ty + 18, Graphics.FONT_XTINY,
                    Fit.toWidth(dc, line.label, "", Graphics.FONT_XTINY, maxW + 8),
                    Graphics.TEXT_JUSTIFY_LEFT | Graphics.TEXT_JUSTIFY_VCENTER);
        dc.drawText(lx, ty + 36, Graphics.FONT_XTINY, Model.paceText(pace, line, now),
                    Graphics.TEXT_JUSTIFY_LEFT | Graphics.TEXT_JUSTIFY_VCENTER);
    }

    // One dot per provider in the narrow band above the headline text. The
    // band is too short for "1/3" in this font but wide enough for dots.
    function drawDots(dc as Graphics.Dc, sub as Graphics.BoundingBox, n as Number) as Void {
        if (n < 2) { return; }
        var cx = subX(sub) / 2 + 7;
        var y = 14;
        for (var i = 0; i < n; i++) {
            var x = cx + (i * 10) - ((n - 1) * 5);
            if (i == mIndex) { dc.fillCircle(x, y, 3); } else { dc.drawCircle(x, y, 2); }
        }
    }

    // Remaining windows as rows: label and figure on one line, a thin bar
    // under them. No pace text here -- at ~9px a character there is no room
    // for label, figure and pace on 176px, and the pace that matters is the
    // headline's.
    function drawRows(dc as Graphics.Dc, rows as Array<Model.Line>, now as Number,
                      top as Number, bottom as Number) as Void {
        var n = rows.size();
        if (n == 0) { return; }
        var pitch = (bottom - top) / n;
        if (pitch > 26) { pitch = 26; }
        var lx = 16;
        var rx = dc.getWidth() - 16;

        for (var i = 0; i < n; i++) {
            var l = rows[i];
            var y = top + (i * pitch);
            var pct = l.remainingPct();
            var figure = pct.toString() + "%";
            var figureW = dc.getTextWidthInPixels(figure, Graphics.FONT_XTINY);

            dc.drawText(lx, y + 8, Graphics.FONT_XTINY,
                        Fit.toWidth(dc, l.label, "", Graphics.FONT_XTINY, rx - lx - figureW - 8),
                        Graphics.TEXT_JUSTIFY_LEFT | Graphics.TEXT_JUSTIFY_VCENTER);
            dc.drawText(rx, y + 8, Graphics.FONT_XTINY, figure,
                        Graphics.TEXT_JUSTIFY_RIGHT | Graphics.TEXT_JUSTIFY_VCENTER);

            // Bar shows REMAINING, matching the board -- a full bar means a full
            // budget, which is what you want to read at a glance.
            var bh = pitch >= 26 ? 5 : 3;
            var by = y + pitch - bh - 2;
            var bw = rx - lx;
            dc.drawRectangle(lx, by, bw, bh);
            var fill = (bw - 2) * pct / 100;
            if (fill > 0) { dc.fillRectangle(lx + 1, by + 1, fill, bh - 2); }
        }
    }
}

// SELECT cycles providers -- the watch equivalent of tapping the board's screen.
class ClaudeBoyDelegate extends WatchUi.BehaviorDelegate {
    var mView as ClaudeBoyView;

    function initialize(view as ClaudeBoyView) {
        BehaviorDelegate.initialize();
        mView = view;
    }

    function onSelect() as Boolean { mView.nextProvider(); return true; }
    function onNextPage() as Boolean { mView.nextProvider(); return true; }
    function onMenu() as Boolean { mView.refresh(); return true; }
}
