import Toybox.Lang;
import Toybox.Background;
import Toybox.System;

// Everything reachable from the service needs (:background) or the compiler
// slices it out -- including the AppBase subclass. Services are killed if they
// do not exit within 30 seconds.
(:background)
class ClaudeBoyService extends System.ServiceDelegate {
    function initialize() { ServiceDelegate.initialize(); }

    function onTemporalEvent() as Void {
        Fetch.start(method(:onResponse));
    }

    function onResponse(code as Number, data as Dictionary or Null) as Void {
        if (code == 200 && data != null) {
            Background.exit(data);          // only the last exit payload reaches the app
        } else {
            Background.exit(null);
        }
    }
}
