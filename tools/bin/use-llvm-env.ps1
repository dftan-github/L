# Enter VS + LLVM 22 env for this repo (same layout as L 2026-11-11).
# Usage:
#   . .\tools\bin\use-llvm-env.ps1
#   Invoke-LlvmEnv 'cmake --version'
#   Invoke-LlvmEnv 'clang++ --version'
$ErrorActionPreference = 'Stop'

$script:LRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$script:LlvmRoot = 'D:\Librarys\clang+llvm-22.1.0-x86_64-pc-windows-msvc'
$script:Vcvars = 'C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat'
$script:CmakeBin = 'C:\Program Files\CMake\bin'

if (-not (Test-Path (Join-Path $script:LlvmRoot 'bin\clang++.exe'))) {
  throw "LLVM not found: $script:LlvmRoot"
}
if (-not (Test-Path $script:Vcvars)) {
  throw "vcvars64.bat not found: $script:Vcvars"
}

$env:PATH = "$script:CmakeBin;$($script:LlvmRoot)\bin;$env:PATH"
$env:LLVM_DIR = Join-Path $script:LlvmRoot 'lib\cmake\llvm'
$env:CC = Join-Path $script:LlvmRoot 'bin\clang.exe'
$env:CXX = Join-Path $script:LlvmRoot 'bin\clang++.exe'

function Invoke-LlvmEnv([string]$command) {
  $prefix = "set PATH=$script:CmakeBin;$($script:LlvmRoot)\bin;%PATH% && set LLVM_DIR=$($script:LlvmRoot)\lib\cmake\llvm &&"
  cmd /c "call `"$script:Vcvars`" >nul && $prefix $command"
  $script:LlvmEnvExit = $LASTEXITCODE
  return $script:LlvmEnvExit
}

Write-Host "L root:  $script:LRoot"
Write-Host "LLVM:    $script:LlvmRoot"
Write-Host "LLVM_DIR:$env:LLVM_DIR"
Write-Host "CXX:     $env:CXX"
