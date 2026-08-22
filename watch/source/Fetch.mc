import Toybox.Lang;
import Toybox.Communications;
import Toybox.Application;

// Used by the background service and by the foreground view on open. Not in the
// glance slice: a glance reads the cached snapshot rather than spending BLE.
(:background)
module Fetch {

    // The watch variant is ~972 bytes. Garmin moves 400-800 bytes/s over BLE, so
    // this is 1-2 seconds; asking for the cyd variant instead would be 3.5KB and
    // four-plus seconds for a chart this screen cannot draw.
    function start(cb as Method(code as Number, data as Dictionary or Null) as Void) as Void {
        // Settings win when set, so the token can be rotated from Connect Mobile
        // without a rebuild -- but fall back to the compiled-in value, because a
        // stored empty setting from a previous install beats any new default and
        // would otherwise leave the widget permanently unconfigured.
        var url = pick(Application.Properties.getValue("ApiUrl"), Secrets.API_URL);
        var token = pick(Application.Properties.getValue("ReadToken"), Secrets.READ_TOKEN);
        if (url.length() == 0 || token.length() == 0) {
            cb.invoke(-9999, null);
            return;
        }
        var options = {
            :method => Communications.HTTP_REQUEST_METHOD_GET,
            :headers => {
                "Authorization" => "Bearer " + token
            },
            :responseType => Communications.HTTP_RESPONSE_CONTENT_TYPE_JSON
        };
        Communications.makeWebRequest(url + "?client=watch", null, options, cb);
    }

    function pick(v as Object or Null, dflt as String) as String {
        if (v instanceof String && (v as String).length() > 0) { return v as String; }
        return dflt;
    }

    // BLE_CONNECTION_UNAVAILABLE (-104) is a normal, frequent state on a watch --
    // the phone is out of range constantly. It is not an error worth shouting about.
    function isExpectedOffline(code as Number) as Boolean {
        return code == -104 || code == -2 || code == -3 || code == -300;
    }
}
