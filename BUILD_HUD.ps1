$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
  & idf.py -D IDF_TARGET=esp32s3 -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.hud.defaults' build
  if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
} finally { Pop-Location }
