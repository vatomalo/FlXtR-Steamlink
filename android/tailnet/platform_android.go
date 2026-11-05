package flxtrnet

import "tailscale.com/net/netmon"

func updateDefaultInterface(name string){netmon.UpdateLastKnownDefaultRouteInterface(name)}
