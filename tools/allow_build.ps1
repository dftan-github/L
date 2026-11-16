# tools/allow_build.ps1
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "=====================================================" -ForegroundColor Red
    Write-Host " [ERROR] 需要管理员权限才能修改 Windows Defender 排除项" -ForegroundColor Red
    Write-Host " 请右键点击 PowerShell，选择 '以管理员身份运行' 后重试。" -ForegroundColor Red
    Write-Host "=====================================================" -ForegroundColor Red
    Exit 1
}

Write-Host "[INFO] 已获得管理员权限，开始配置 Defender 排除项..." -ForegroundColor Cyan

$projectRoot = Resolve-Path "$PSScriptRoot\.." | Select-Object -ExpandProperty Path

$pathsToExclude = @(
    "$projectRoot\build",
    "$projectRoot\test_out",
    "$projectRoot\build\clang-debug\compiler\l.exe"
)

foreach ($path in $pathsToExclude) {
    if (Test-Path $path) {
        try {
            Add-MpPreference -ExclusionPath $path -ErrorAction Stop
            Write-Host "[OK] 已排除: $path" -ForegroundColor Green
        } catch {
            Write-Host "[WARN] 排除失败: $path - $_" -ForegroundColor Yellow
        }
    } else {
        Write-Host "[SKIP] 路径不存在，跳过: $path" -ForegroundColor DarkGray
    }
}

Write-Host "[INFO] 配置完成！以后的 L 编译器测试将不再受 Defender 弹窗干扰。" -ForegroundColor Cyan
