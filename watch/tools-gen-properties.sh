#!/bin/sh
# Regenerates resources/settings/properties.xml with the real token baked in as the
# DEFAULT. Settings in Connect Mobile still override it -- this just means a
# sideloaded build works without needing the phone to hand it a token first.
# The generated file is git-ignored; only properties.xml.example is committed.
set -e
. "$HOME/work/envs/claudeboy.env"
cd "$(dirname "$0")"
cat > resources/settings/properties.xml <<XML
<properties>
  <property id="ApiUrl" type="string">${CLAUDEBOY_URL}/v1/snapshot</property>
  <property id="ReadToken" type="string">${CLAUDEBOY_READ_TOKEN}</property>
</properties>
XML
echo "generated resources/settings/properties.xml"
