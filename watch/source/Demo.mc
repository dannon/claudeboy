import Toybox.Lang;
import Toybox.Timer;

// Simulator-only: cycle providers on a timer, because the simulator cannot be
// driven by scripted keystrokes without Accessibility rights on the host.
// monkey.jungle excludes :sim, so none of this reaches the watch; sim.jungle
// excludes :nosim instead.
(:sim)
module Demo {
    var timer as Timer.Timer or Null = null;

    function start(view as ClaudeBoyView) as Void {
        if (timer != null) { return; }
        timer = new Timer.Timer();
        timer.start(view.method(:nextProvider), 6000, true);
    }
}

(:nosim)
module Demo {
    function start(view as ClaudeBoyView) as Void {}
}
