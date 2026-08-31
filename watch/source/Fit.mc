import Toybox.Lang;
import Toybox.Graphics;

// Text that must not run under the subscreen bezel. Shared with the glance, so
// it is charged against the glance budget -- keep it tiny.
(:glance)
module Fit {
    // Words OpenUsage puts in window labels, and what to call them when the
    // full label will not fit: "Claude Wk" reads, "Claude Wee" does not.
    const SHORT = ["Weekly", "Wk", "Session", "Sess", "Monthly", "Mo"];

    // Make s + tail fit in maxW pixels: as written if it fits, else with the
    // long words shortened, else cut one character at a time. Never returns
    // less than one character of s, so a label is trimmed, never lost.
    function toWidth(dc as Graphics.Dc, s as String, tail as String,
                     font as Graphics.FontDefinition, maxW as Number) as String {
        var t = s;
        if (dc.getTextWidthInPixels(t + tail, font) > maxW) {
            for (var i = 0; i < SHORT.size(); i += 2) {
                t = swap(t, SHORT[i] as String, SHORT[i + 1] as String);
            }
        }
        while (t.length() > 1 && dc.getTextWidthInPixels(t + tail, font) > maxW) {
            t = t.substring(0, t.length() - 1);
        }
        return t + tail;
    }

    function swap(s as String, from as String, to as String) as String {
        var at = s.find(from);
        if (at == null) { return s; }
        return s.substring(0, at) + to + s.substring(at + from.length(), s.length());
    }
}
