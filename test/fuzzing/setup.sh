#!/bin/bash

# Setzt die Umgebungsvariable für die Quelldatei
export KCONFIG_CONFIG=fuzz.config

# Generiert die finale .config und die config.h für den Compiler
python3 genconfig.py --header-path src/generated/config.h --config-out .config Kconfig