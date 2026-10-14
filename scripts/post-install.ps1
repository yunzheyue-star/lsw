# LSW 安装后检查脚本（在 Windows PowerShell 中以管理员身份运行）
# 检查 OpenSSH Server、QEMU Guest Agent、SMB 与 Z: 映射。

$ErrorActionPreference = "Continue"
Write-Host "=== LSW post-install 检查 ===" -ForegroundColor Cyan

# 1. OpenSSH Server
$sshd = Get-Service -Name sshd -ErrorAction SilentlyContinue
if ($null -ne $sshd) {
    Write-Host "[OK] OpenSSH Server 已安装，状态：$($sshd.Status)" -ForegroundColor Green
    if ($sshd.Status -ne "Running") {
        Write-Host "     请执行：Start-Service sshd; Set-Service -Name sshd -StartupType Automatic"
    }
} else {
    Write-Host "[TODO] 未检测到 sshd，请用管理员 PowerShell 执行：" -ForegroundColor Yellow
    Write-Host "       Add-WindowsCapability -Online -Name OpenSSH.Server~~~~0.0.1.0"
    Write-Host "       Start-Service sshd; Set-Service -Name sshd -StartupType Automatic"
}

# 2. QEMU Guest Agent
$qga = Get-Service -Name qemu-ga -ErrorAction SilentlyContinue
if ($null -ne $qga) {
    Write-Host "[OK] QEMU Guest Agent 已安装，状态：$($qga.Status)" -ForegroundColor Green
    if ($qga.Status -ne "Running") {
        Write-Host "     请执行：Start-Service qemu-ga"
    }
} else {
    Write-Host "[TODO] 未检测到 qemu-ga：请从 virtio-win ISO 安装 Guest Agent" -ForegroundColor Yellow
}

# 3. SMB Server / 445 端口
$smb = Get-Service -Name LanmanServer -ErrorAction SilentlyContinue
if ($null -ne $smb -and $smb.Status -eq "Running") {
    Write-Host "[OK] SMB Server（LanmanServer）正在运行" -ForegroundColor Green
} else {
    Write-Host "[TODO] 请启动 SMB Server：Start-Service LanmanServer" -ForegroundColor Yellow
}

# 4. Z: 映射到 Linux $HOME（QEMU user SMB）
if (Test-Path "Z:\") {
    Write-Host "[OK] Z: 已映射" -ForegroundColor Green
    Get-PSDrive -Name Z | Format-List Name, Root, DisplayRoot
} else {
    Write-Host "[TODO] 请映射 Z:：net use Z: \\10.0.2.4\qemu /persistent:yes" -ForegroundColor Yellow
}

Write-Host "=== 检查结束，请按 [TODO] 项逐条处理 ===" -ForegroundColor Cyan
