import Toybox.Lang;
import Toybox.WatchUi;
import Toybox.Graphics;
import Toybox.System;

// Glance mode gets 32KB (about 28KB usable once Connect IQ takes its cut), and
// GlanceView supports no Layers and no page control -- draw straight to the Dc.
(:glance)
class ClaudeBoyGlance extends WatchUi.GlanceView {

    function initialize() { GlanceView.initialize(); }

    function onUpdate(dc as Graphics.Dc) as Void {
        dc.setColor(Graphics.COLOR_WHITE, Graphics.COLOR_TRANSPARENT);
        var h = dc.getHeight();
        var snap = Cache.load();


        // The glance band runs under the subscreen bezel on its right-hand
        // side: both lines have to stay inside the left 55% or so of the width.
        var x = 4;
        var maxW = (dc.getWidth() * 11 / 20) - x;

        if (snap == null) {
            dc.drawText(x, h / 2, Graphics.FONT_TINY, "CLAUDEBOY  --",
                        Graphics.TEXT_JUSTIFY_LEFT | Graphics.TEXT_JUSTIFY_VCENTER);
            return;
        }

        var now = snap.nowSec();
        // Lead with whatever is closest to running out: that is the number worth
        // surfacing without opening the widget.
        var worst = null;
        var worstPct = 101;
        for (var i = 0; i < snap.providers.size(); i++) {
            var p = snap.providers[i];
            for (var j = 0; j < p.lines.size(); j++) {
                var pct = p.lines[j].remainingPct();
                if (pct < worstPct) { worstPct = pct; worst = p.lines[j]; }
            }
        }

        var stale = Model.stalenessWith(snap, true);
        var top = (worst == null) ? "CLAUDEBOY"
                : Fit.toWidth(dc, worst.label, " " + worstPct.toString() + "%", Graphics.FONT_TINY, maxW);
        dc.drawText(x, h / 3, Graphics.FONT_TINY, top,
                    Graphics.TEXT_JUSTIFY_LEFT | Graphics.TEXT_JUSTIFY_VCENTER);

        var sub = stale.length() > 0 ? stale
                : (worst == null ? ""
                : Model.paceWord(worst.pace(now), Model.coarseDuration(worst.resetInSec(now))));
        dc.drawText(x, (h * 2) / 3, Graphics.FONT_XTINY,
                    Fit.toWidth(dc, sub, "", Graphics.FONT_XTINY, maxW + 8),
                    Graphics.TEXT_JUSTIFY_LEFT | Graphics.TEXT_JUSTIFY_VCENTER);

    }
}
