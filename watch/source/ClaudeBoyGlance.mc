import Toybox.Lang;
import Toybox.WatchUi;
import Toybox.Graphics;

// Glance mode gets 32KB (about 28KB usable once Connect IQ takes its cut), and
// GlanceView supports no Layers and no page control -- draw straight to the Dc.
(:glance)
class ClaudeBoyGlance extends WatchUi.GlanceView {

    function initialize() { GlanceView.initialize(); }

    function onUpdate(dc as Graphics.Dc) as Void {
        dc.setColor(Graphics.COLOR_WHITE, Graphics.COLOR_TRANSPARENT);
        var h = dc.getHeight();
        var snap = Cache.load();

        if (snap == null) {
            dc.drawText(0, h / 2, Graphics.FONT_TINY, "CLAUDEBOY  --",
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

        var stale = Model.stalenessText(snap);
        var top = (worst == null) ? "CLAUDEBOY" : worst.label + "  " + worstPct.toString() + "%";
        dc.drawText(0, h / 3, Graphics.FONT_TINY, top,
                    Graphics.TEXT_JUSTIFY_LEFT | Graphics.TEXT_JUSTIFY_VCENTER);

        var sub = stale.length() > 0 ? stale
                : (worst == null ? "" : Model.paceText(worst.pace(now), worst, now));
        dc.drawText(0, (h * 2) / 3, Graphics.FONT_XTINY, sub,
                    Graphics.TEXT_JUSTIFY_LEFT | Graphics.TEXT_JUSTIFY_VCENTER);
    }
}
