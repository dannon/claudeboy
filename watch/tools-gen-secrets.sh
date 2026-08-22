#!/bin/sh
# Generates source/Secrets.mc from ~/work/envs/claudeboy.env.
# Git-ignored; only Secrets.mc.example is committed.
#
# Why a compiled-in literal and not just the properties.xml default: a stored
# settings value beats a default, and a sideloaded app that has ever been opened
# has stored settings. So a fresh default never reaches an already-installed app.
set -e
. "$HOME/work/envs/claudeboy.env"
cd "$(dirname "$0")"
cat > source/Secrets.mc <<MC
import Toybox.Lang;

// GENERATED -- do not edit, do not commit. Run ./tools-gen-secrets.sh
(:glance :background)
module Secrets {
    const API_URL = "${CLAUDEBOY_URL}/v1/snapshot";
    const READ_TOKEN = "${CLAUDEBOY_READ_TOKEN}";
}
MC
echo "generated source/Secrets.mc"
