import Toybox.Lang;
import Toybox.System;
import Toybox.Time;

// Shared by the glance slice, the background service and the full view, so it
// carries both annotations. Keep it small: everything in here is charged against
// the 32KB glance budget, not the 64KB widget one.
(:glance :background)
module Model {

    // Mirrors cb::PaceState in the board's core/pace.h. Both clients compute pace
    // from raw facts so the reading stays live between polls instead of freezing.
    enum {
        PACE_UNKNOWN = 0,
        PACE_SURPLUS = 1,
        PACE_ON = 2,
        PACE_BURNOUT = 3,
        PACE_READY = 4      // window has not started -- no resetsAt on the wire
    }

    // Same thresholds as core/pace.h. Changing one here without the other makes
    // the watch and the board disagree about the same numbers.
    const BURNOUT_RATIO = 1.02;
    const SURPLUS_RATIO = 0.90;
    const MIN_ELAPSED_FRAC = 0.05;

    const STALE_AFTER_SEC = 600;      // 10 minutes
    const LOST_AFTER_SEC = 7200;      // 2 hours

    class Line {
        var label as String = "";
        var used as Number = 0;
        var limit as Number = 100;
        var resetsAt as Number or Null = null;   // absent = window not started
        var periodSec as Number = 0;

        function remainingPct() as Number {
            if (limit <= 0) { return 0; }
            var r = 100 - ((used * 100) / limit);
            if (r < 0) { r = 0; }
            if (r > 100) { r = 100; }
            return r;
        }

        // nowSec is the server clock carried forward locally, not the watch clock.
        function pace(nowSec as Number) as Number {
            if (resetsAt == null || periodSec <= 0 || limit <= 0) {
                return PACE_READY;
            }
            var toReset = resetsAt - nowSec;
            if (toReset < 0) { toReset = 0; }
            var elapsed = (periodSec - toReset).toFloat() / periodSec.toFloat();
            if (elapsed < MIN_ELAPSED_FRAC) { return PACE_UNKNOWN; }
            var usedFrac = used.toFloat() / limit.toFloat();
            var ratio = usedFrac / elapsed;
            if (ratio > BURNOUT_RATIO) { return PACE_BURNOUT; }
            if (ratio < SURPLUS_RATIO) { return PACE_SURPLUS; }
            return PACE_ON;
        }

        function resetInSec(nowSec as Number) as Number {
            if (resetsAt == null) { return 0; }
            var d = resetsAt - nowSec;
            return d > 0 ? d : 0;
        }
    }

    class Provider {
        var id as String = "";
        var displayName as String = "";
        var plan as String = "";
        var fetchedAt as Number = 0;
        var lines as Array<Line> = [] as Array<Line>;
    }

    class Snapshot {
        var serverTime as Number = 0;
        var providers as Array<Provider> = [] as Array<Provider>;
        // Monotonic ms at the moment this snapshot arrived, so the clock can be
        // carried forward locally between fetches -- same trick the board uses.
        var receivedMs as Number = 0;

        function isEmpty() as Boolean { return providers.size() == 0; }

        // Server time advanced by however long we have been holding this.
        function nowSec() as Number {
            var elapsed = (System.getTimer() - receivedMs) / 1000;
            if (elapsed < 0) { elapsed = 0; }
            return serverTime + elapsed;
        }

        function newestFetchedAt() as Number {
            var best = 0;
            for (var i = 0; i < providers.size(); i++) {
                if (providers[i].fetchedAt > best) { best = providers[i].fetchedAt; }
            }
            return best;
        }

        // Age of the freshest provider data, in seconds.
        function ageSec() as Number {
            var f = newestFetchedAt();
            if (f == 0) { return -1; }          // never fetched
            var a = nowSec() - f;
            return a > 0 ? a : 0;
        }

        function byId(wanted as String) as Provider or Null {
            for (var i = 0; i < providers.size(); i++) {
                if (providers[i].id.equals(wanted)) { return providers[i]; }
            }
            return null;
        }
    }

    // Build a Snapshot from the Dictionary makeWebRequest hands back. Total by
    // design: anything malformed is skipped rather than thrown on, because this
    // runs unattended and one bad field should cost that field, not the screen.
    function fromDict(d as Dictionary or Null) as Snapshot or Null {
        if (d == null) { return null; }
        var st = d["serverTime"];
        var ps = d["providers"];
        if (!(st instanceof Number) || !(ps instanceof Array)) { return null; }

        var snap = new Snapshot();
        snap.serverTime = st as Number;
        snap.receivedMs = System.getTimer();

        var out = [] as Array<Provider>;
        for (var i = 0; i < ps.size(); i++) {
            var raw = ps[i];
            if (!(raw instanceof Dictionary)) { continue; }
            var p = new Provider();
            p.id = str(raw["id"]);
            p.displayName = str(raw["displayName"]);
            p.plan = str(raw["plan"]);
            p.fetchedAt = num(raw["fetchedAt"], 0);
            if (p.id.length() == 0) { continue; }

            var pl = raw["progress"];
            var ls = [] as Array<Line>;
            if (pl instanceof Array) {
                for (var j = 0; j < pl.size(); j++) {
                    var rl = pl[j];
                    if (!(rl instanceof Dictionary)) { continue; }
                    var l = new Line();
                    l.label = str(rl["label"]);
                    l.used = num(rl["used"], -1);
                    l.limit = num(rl["limit"], 0);
                    l.periodSec = num(rl["periodSec"], 0);
                    // Absent resetsAt is a real state, not an error: the window
                    // has not started. Dropping the line here is exactly the bug
                    // that made the board's Session card vanish.
                    var r = rl["resetsAt"];
                    l.resetsAt = (r instanceof Number) ? r as Number : null;
                    if (l.label.length() == 0 || l.used < 0 || l.limit < 1) { continue; }
                    ls.add(l);
                }
            }
            p.lines = ls;
            out.add(p);
        }
        if (out.size() == 0) { return null; }
        snap.providers = out;
        return snap;
    }

    function str(v as Object or Null) as String {
        return (v instanceof String) ? v as String : "";
    }

    function num(v as Object or Null, dflt as Number) as Number {
        return (v instanceof Number) ? v as Number : dflt;
    }

    function paceText(p as Number, line as Line, nowSec as Number) as String {
        if (p == PACE_READY) { return "READY"; }
        if (p == PACE_UNKNOWN) { return ""; }
        var d = shortDuration(line.resetInSec(nowSec));
        if (p == PACE_BURNOUT) { return "BURN " + d; }
        if (p == PACE_SURPLUS) { return "SPARE " + d; }
        return "ON " + d;
    }

    function shortDuration(sec as Number) as String {
        if (sec <= 0) { return "0m"; }
        var d = sec / 86400;
        if (d > 0) { return d.toString() + "d" + ((sec % 86400) / 3600).toString() + "h"; }
        var h = sec / 3600;
        if (h > 0) { return h.toString() + "h" + ((sec % 3600) / 60).toString() + "m"; }
        return (sec / 60).toString() + "m";
    }

    // Same four states as the board. Never blank the screen -- dim and show the age.
    function stalenessText(snap as Snapshot or Null) as String {
        if (snap == null) { return "NO SIGNAL"; }
        var a = snap.ageSec();
        if (a < 0) { return "NO SIGNAL"; }
        if (a < STALE_AFTER_SEC) { return ""; }
        if (a <= LOST_AFTER_SEC) { return "STALE " + shortDuration(a); }
        return "LOST " + shortDuration(a);
    }
}
