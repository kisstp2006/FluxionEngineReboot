# The contents of this file are subject to the Common Public Attribution
# License Version 1.0 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# https://opensource.org/license/cpal-1-0. The License is based on the
# Mozilla Public License Version 1.1 but Sections 14 and 15 have been added
# to cover use of software over a computer network and provide for limited
# attribution for the Original Developer. In addition, Exhibit A has been
# modified to be consistent with Exhibit B.
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is Fluxion Engine.
#
# The Original Developer is not the Initial Developer and is __________. If
# left blank, the Original Developer is the Initial Developer.
#
# The Initial Developer of the Original Code is Kiss Tibor Péter. All
# portions of the code written by Kiss Tibor Péter are Copyright (c) 2026.
# All Rights Reserved.
#
# Contributor ______________________.
#
# Alternatively, the contents of this file may be used under the terms of
# the Fluxion Engine Commercial License Agreement Version 1.0, separately
# obtained from and valid as granted by Kiss Tibor Péter (the "Commercial
# License"), in which case the provisions of the Commercial License are
# applicable instead of those above.
#
# If you wish to allow use of your version of this file only under the terms
# of the Commercial License and not to allow others to use your version of
# this file under the CPAL, indicate your decision by deleting the
# provisions above and replace them with the notice and other provisions
# required by the Commercial License. If you do not delete the provisions
# above, a recipient may use your version of this file under either the CPAL
# or the Commercial License.
#
# SPDX-License-Identifier: CPAL-1.0

# Building and testing on Windows, fast.
#
#   Tools\windows.ps1 setup              install/check what building needs
#   Tools\windows.ps1 build [debug|release|all]
#   Tools\windows.ps1 test  [debug|release] [-- extra ctest args]
#   Tools\windows.ps1 vs                 generate the Visual Studio solution
#   Tools\windows.ps1 demo  [vulkan|d3d12|opengl]
#
# The fast builds use Ninja + cl + ccache instead of MSBuild -- measured
# here: a no-op build in a fifth of a second, a clean rebuild in ~10
# seconds warm, where the solution build took minutes. The Visual Studio
# solution still exists for the IDE (`vs`); these trees are for building.
#
# THE FAST TREES LIVE OUTSIDE THE REPOSITORY, at ~\.fluxion\ -- not for
# tidiness: the repository's own path is deep enough that CMake's hashed
# object paths plus ccache's temp suffix cross Windows' 260-character
# path limit, and the failure is cl reporting it cannot open a file with
# no name. A short root is the fix that needs no admin rights.
#
# ccache notes, so nobody re-learns them: debug info must be embedded
# (/Z7 -- ccache cannot cache /Zi's shared PDB writes), MSVC precompiled
# headers are off (unsupported by ccache), and C++ module scanning is off
# (the scan step breaks behind a launcher; nothing here uses modules).

param(
    [Parameter(Position = 0)] [string]$Command = "build",
    [Parameter(Position = 1, ValueFromRemainingArguments = $true)] [string[]]$Rest
)

$ErrorActionPreference = "Stop"
$Source = Split-Path -Parent $PSScriptRoot
$FastRoot = Join-Path $env:USERPROFILE ".fluxion"

function Enter-DevShell {
    if (Get-Command cl -ErrorAction SilentlyContinue) { return }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "Visual Studio was not found. Run: Tools\windows.ps1 setup"
    }
    $vs = & $vswhere -latest -property installationPath
    Import-Module (Join-Path $vs "Common7\Tools\Microsoft.VisualStudio.DevShell.dll")
    # All streams silenced: the dev shell's own bootstrap prints a
    # harmless complaint about vswhere not being on PATH.
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64" *> $null
}

function Configure-Tree([string]$Tree, [string]$Type) {
    if (Test-Path (Join-Path $Tree "CMakeCache.txt")) { return }
    $launcher = @()
    if (Get-Command ccache -ErrorAction SilentlyContinue) {
        $launcher = @(
            "-DCMAKE_C_COMPILER_LAUNCHER=ccache",
            "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache",
            "-DCMAKE_POLICY_DEFAULT_CMP0141=NEW",
            "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded",
            "-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON")
    }
    cmake -S $Source -B $Tree -G Ninja "-DCMAKE_BUILD_TYPE=$Type" `
        -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl `
        -DCMAKE_CXX_SCAN_FOR_MODULES=OFF @launcher | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "configure failed for $Tree" }
}

function Build-Tree([string]$Tree, [string]$Type) {
    Configure-Tree $Tree $Type
    cmake --build $Tree -j $env:NUMBER_OF_PROCESSORS
    if ($LASTEXITCODE -ne 0) { throw "build failed for $Tree" }
}

function Pick-Config {
    $config = "debug"
    if ($Rest.Count -gt 0 -and @("debug", "release", "all") -contains $Rest[0]) {
        $config = $Rest[0]
        $script:Rest = $Rest[1..($Rest.Count - 1)]
    }
    return $config
}

switch ($Command) {
    "setup" {
        # Installed quietly where winget can; only detected and named
        # where an install is a decision (Visual Studio is gigabytes and
        # has editions, the Vulkan SDK versions matter).
        foreach ($tool in @(@("cmake", "Kitware.CMake"), @("ninja", "Ninja-build.Ninja"), @("ccache", "Ccache.Ccache"))) {
            if (Get-Command $tool[0] -ErrorAction SilentlyContinue) {
                Write-Host "$($tool[0]): ok"
            } else {
                Write-Host "$($tool[0]): installing via winget..."
                winget install --id $tool[1] --silent --accept-package-agreements --accept-source-agreements
            }
        }
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $vswhere) {
            Write-Host "Visual Studio: $(& $vswhere -latest -property displayName)"
        } else {
            Write-Host "Visual Studio: MISSING -- install it (Community is fine) with the 'Desktop development with C++' workload."
        }
        if ($env:VULKAN_SDK) {
            Write-Host "Vulkan SDK: $env:VULKAN_SDK"
        } else {
            Write-Host "Vulkan SDK: MISSING -- winget install KhronosGroup.VulkanSDK (then reopen the terminal)."
        }
    }

    "build" {
        Enter-DevShell
        $config = Pick-Config
        if ($config -eq "debug" -or $config -eq "all") { Build-Tree (Join-Path $FastRoot "wd") "Debug" }
        if ($config -eq "release" -or $config -eq "all") { Build-Tree (Join-Path $FastRoot "wr") "Release" }
    }

    "test" {
        Enter-DevShell
        $config = Pick-Config
        $trees = @()
        if ($config -eq "debug" -or $config -eq "all") { $trees += ,@((Join-Path $FastRoot "wd"), "Debug") }
        if ($config -eq "release" -or $config -eq "all") { $trees += ,@((Join-Path $FastRoot "wr"), "Release") }
        foreach ($pair in $trees) {
            Build-Tree $pair[0] $pair[1]
            Push-Location $pair[0]
            try {
                ctest -j $env:NUMBER_OF_PROCESSORS --output-on-failure @Rest
                if ($LASTEXITCODE -ne 0) { throw "tests failed in $($pair[0])" }
            } finally {
                Pop-Location
            }
        }
    }

    "vs" {
        # The solution for working in the IDE. MSBuild is the slow way to
        # BUILD; it is still the right way to debug and browse.
        cmake --preset windows-msvc
        if ($LASTEXITCODE -ne 0) { throw "configure failed" }
        Write-Host ""
        Write-Host "Solution: $Source\build\windows-msvc\FluxionEngine.slnx"
    }

    "demo" {
        Enter-DevShell
        $backend = "vulkan"
        if ($Rest.Count -gt 0) { $backend = $Rest[0] }
        Build-Tree (Join-Path $FastRoot "wd") "Debug"
        & (Join-Path $FastRoot "wd\Samples\ForwardRendererDemo\ForwardRendererDemo.exe") "--graphics=$backend"
    }

    default {
        Write-Host "Commands: setup | build [debug|release|all] | test [debug|release] [ctest args] | vs | demo [backend]"
    }
}
