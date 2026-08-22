import Toybox.Lang;
import Toybox.Application;
import Toybox.Background;
import Toybox.System;
import Toybox.Time;
import Toybox.WatchUi;

// Annotated so the glance slice has an entry point. getInitialView deliberately
// is NOT annotated -- the full view must stay out of the 32KB glance budget.
(:glance :background)
class ClaudeBoyApp extends Application.AppBase {

    function initialize() { AppBase.initialize(); }

    function onStart(state as Dictionary?) as Void {
        // 5 minutes is Garmin's floor and matches how often OpenUsage refreshes,
        // so asking more often would spend BLE on data that has not changed.
        if (Toybox.System has :ServiceDelegate) {
            var due = Background.getTemporalEventRegisteredTime();
            if (due == null) {
                Background.registerForTemporalEvent(new Time.Duration(5 * 60));
            }
        }
    }

    function getServiceDelegate() {
        return [new ClaudeBoyService()];
    }

    // The service cannot write app properties, so it hands data up and the
    // foreground is what persists it.
    function onBackgroundData(data as Application.PersistableType) as Void {
        if (data != null) {
            Storage.setValue("snap", data);
            Storage.setValue("snapAt", Time.now().value());
        }
        WatchUi.requestUpdate();
    }

    function getInitialView() {
        var v = new ClaudeBoyView();
        return [v, new ClaudeBoyDelegate(v)];
    }

    (:glance)
    function getGlanceView() {
        return [new ClaudeBoyGlance()];
    }
}

// Rehydrate whatever the last successful fetch stored. Shared by the glance and
// the full view so they never disagree about what is on screen.
(:glance :background)
module Cache {
    function load() as Model.Snapshot or Null {
        var d = Storage.getValue("snap");
        if (!(d instanceof Dictionary)) { return null; }
        var snap = Model.fromDict(d as Dictionary);
        if (snap == null) { return null; }
        // serverTime came from the fetch; re-anchor it to when we stored it so
        // the age is honest across a widget restart rather than reading as fresh.
        var at = Storage.getValue("snapAt");
        if (at instanceof Number) {
            var heldSec = Time.now().value() - (at as Number);
            if (heldSec > 0) { snap.serverTime += heldSec; }
        }
        return snap;
    }
}
