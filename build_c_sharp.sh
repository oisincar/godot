#!/usr/bin/env bash

# First build
scons p=x11 tools=yes module_mono_enabled=yes mono_glue=no -j8
# Mono glue
bin/godot.x11.tools.64.mono --generate-mono-glue modules/mono/glue
# Final build
scons p=x11 target=release_debug tools=yes module_mono_enabled=yes -j8

