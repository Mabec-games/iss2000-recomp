# Package the Windows build (tools\build_windows.bat) as a zip: the exe plus the DLLs it needs next to it.
#   powershell -ExecutionPolicy Bypass -File tools\make_windows_zip.ps1 [build dir, default build-win]
# Output: dist\Mabecs_ISS2000_Widescreen-Windows-x64.zip
# Relies on Windows 10/11 for D3D12 (or a Vulkan driver) and the Universal C runtime.
param([string]$BuildDir = "build-win")
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Build = Join-Path $Root $BuildDir
$Stage = Join-Path $Root "dist\Mabecs_ISS2000_Widescreen"
$Out = Join-Path $Root "dist\Mabecs_ISS2000_Widescreen-Windows-x64.zip"

if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Force (Join-Path $Stage "licenses") | Out-Null

Copy-Item (Join-Path $Build "ISS2000Recompiled.exe") $Stage
foreach ($dll in "SDL2.dll", "dxcompiler.dll", "dxil.dll") {
    Copy-Item (Join-Path $Build $dll) $Stage
}
Copy-Item (Join-Path $Root "LICENSE") (Join-Path $Stage "licenses\LICENSE.txt")
Copy-Item (Join-Path $Root "assets\DejaVu-LICENSE.txt") (Join-Path $Stage "licenses\")
Copy-Item (Join-Path $Root "ext\rt64\src\contrib\mupen64plus-win32-deps\SDL2-2.26.3\COPYING.txt") (Join-Path $Stage "licenses\SDL2-COPYING.txt")

# Build to a temporary name and rename over the old file, so a failed run keeps the previous zip.
if (Test-Path "$Out.new") { Remove-Item -Force "$Out.new" }
Compress-Archive -Path $Stage -DestinationPath "$Out.new.zip"
Move-Item -Force "$Out.new.zip" $Out
Remove-Item -Recurse -Force $Stage
Get-Item $Out | Select-Object FullName, Length
