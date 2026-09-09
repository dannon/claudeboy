# Built by hand, not by npm. swiftc's ad-hoc signature is enough for Bluetooth
# TCC -- no developer identity needed -- and the grant survives a rebuild at the
# same path, so this does not cost a re-approval (measured in Task 2).
claudeboy-ble: claudeboy-ble.swift
	swiftc -O $< -o $@

clean:
	rm -f claudeboy-ble

.PHONY: clean
