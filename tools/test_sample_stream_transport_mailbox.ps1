$ErrorActionPreference = 'Stop'

function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
}

$root = Split-Path -Parent $PSScriptRoot
$gccCandidates = @(
    'C:\msys64\ucrt64\bin\gcc.exe',
    'C:\msys64\mingw64\bin\gcc.exe'
)
$gcc = $gccCandidates | Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1
Assert-Contract ($null -ne $gcc) 'Native C compiler unavailable'

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) (
    'brick_sample_stream_mailbox_' + [Guid]::NewGuid().ToString('N'))
$stubRoot = Join-Path $tempRoot 'stubs'
$runtimeExe = Join-Path $tempRoot 'sample_stream_mailbox.exe'
New-Item -ItemType Directory -Path (Join-Path $stubRoot 'SD') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stubRoot 'Platform') -Force | Out-Null
Set-Content -LiteralPath (Join-Path $stubRoot 'ff.h') -Value @'
#pragma once
typedef struct { unsigned int opaque; } FIL;
'@
Set-Content -LiteralPath (Join-Path $stubRoot 'stm32h7xx.h') -Value @'
#pragma once
#define __DCACHE_PRESENT 0
#define __DMB() __asm__ __volatile__("" ::: "memory")
'@
Set-Content -LiteralPath (Join-Path $stubRoot 'Platform/memory_layout.h') -Value @'
#pragma once
#define ALIGN32 __attribute__((aligned(32)))
#define SDRAM_STREAM_SERVICE
'@
Set-Content -LiteralPath (Join-Path $stubRoot 'SD/sd_scheduler_runtime.h') -Value @'
#pragma once
void sd_scheduler_runtime_service(void);
'@

$savedPath = $env:PATH
try {
    $env:PATH = "$(Split-Path -Parent $gcc);$savedPath"
    & $gcc -std=c11 -Wall -Wextra -Werror -Wno-type-limits "-I$stubRoot" `
        "-I$(Join-Path $root 'Inc')" `
        (Join-Path $root 'tools/test_sample_stream_transport_mailbox.c') `
        -o $runtimeExe
    Assert-Contract ($LASTEXITCODE -eq 0) 'Native mailbox test build failed'
    & $runtimeExe
    Assert-Contract ($LASTEXITCODE -eq 0) 'Mailbox reset runtime assertions failed'
} finally {
    $env:PATH = $savedPath
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
