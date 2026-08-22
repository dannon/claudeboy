import Toybox.Lang;
import Toybox.WatchUi;
import Toybox.Graphics;
import Toybox.System;
import Toybox.Application;
import Toybox.Application.Storage;
import Toybox.Time;

class ClaudeBoyView extends WatchUi.View {

    var mIndex as Number = 0;          // which provider is showing
    var mSnap as Model.Snapshot or Null = null;
    var mFetching as Boolean = false;
    var mNote as String = "";

    function initialize() { View.initialize(); }

    function onShow() as Void {
        mSnap = Cache.load();
        refresh();
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

        // Header: provider name, and which of the set this is.
        var head = p.displayName.toUpper();
        if (mSnap.providers.size() > 1) {
            head += "  " + (mIndex + 1).toString() + "/" + mSnap.providers.size().toString();
        }
        dc.drawText(w / 2, 18, Graphics.FONT_XTINY, head,
                    Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER);

        // Gauge rows. Antigravity carries four lines, Codex one, so the row
        // height is derived from the count rather than fixed.
        var n = p.lines.size();
        if (n == 0) { n = 1; }
        var top = 34;
        var bottom = h - 26;
        var rowH = (bottom - top) / n;
        if (rowH > 34) { rowH = 34; }

        for (var i = 0; i < p.lines.size(); i++) {
            var l = p.lines[i];
            var y = top + (i * rowH);
            var pct = l.remainingPct();
            var pace = l.pace(now);

            dc.drawText(14, y + 6, Graphics.FONT_XTINY, l.label,
                        Graphics.TEXT_JUSTIFY_LEFT | Graphics.TEXT_JUSTIFY_VCENTER);
            dc.drawText(w - 14, y + 6, Graphics.FONT_XTINY, pct.toString() + "%",
                        Graphics.TEXT_JUSTIFY_RIGHT | Graphics.TEXT_JUSTIFY_VCENTER);

            // Bar shows REMAINING, matching the board -- a full bar means a full
            // budget, which is what you want to read at a glance.
            var bx = 14;
            var bw = w - 28;
            var by = y + 16;
            dc.drawRectangle(bx, by, bw, 6);
            var fill = (bw - 2) * pct / 100;
            if (fill > 0) { dc.fillRectangle(bx + 1, by + 1, fill, 4); }

            var pt = Model.paceText(pace, l, now);
            if (pt.length() > 0 && rowH >= 30) {
                dc.drawText(bx, by + 12, Graphics.FONT_XTINY, pt,
                            Graphics.TEXT_JUSTIFY_LEFT | Graphics.TEXT_JUSTIFY_VCENTER);
            }
        }

        // Footer: staleness wins over any transient note, because old numbers
        // shown as current is the one thing this display must never do.
        var foot = Model.stalenessText(mSnap);
        if (foot.length() == 0) { foot = mNote; }
        if (foot.length() > 0) {
            dc.drawText(w / 2, h - 16, Graphics.FONT_XTINY, foot,
                        Graphics.TEXT_JUSTIFY_CENTER | Graphics.TEXT_JUSTIFY_VCENTER);
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
