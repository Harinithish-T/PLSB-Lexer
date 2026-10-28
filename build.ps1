param(
    [switch]$Test,
    [switch]$Benchmark,
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$binDir = "bin"
if ($Clean) {
    Write-Host "Cleaning build artifacts..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $binDir -ErrorAction SilentlyContinue
    Remove-Item "src\*.o" -ErrorAction SilentlyContinue
    Write-Host "Clean complete." -ForegroundColor Green
    exit 0
}

if (-not (Test-Path $binDir)) {
    New-Item -ItemType Directory -Path $binDir | Out-Null
}

$gcc = "gcc"
$cflags = @("-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-O2", "-Isrc")
$srcFiles = @(
    "src/input_buffer.c",
    "src/predictor.c",
    "src/spill_buffer.c",
    "src/lexer.c",
    "src/statistics.c",
    "src/report.c"
)

Write-Host "Compiling PLSB core engine and driver..." -ForegroundColor Cyan
& $gcc $cflags "src/main.c" $srcFiles -o "$binDir/plsb_lexer.exe" -lm
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to build plsb_lexer.exe"
}
Write-Host "  [OK] Built $binDir/plsb_lexer.exe" -ForegroundColor Green

Write-Host "Compiling comprehensive master test runner..." -ForegroundColor Cyan
$testSources = @(
    "tests/test_runner.c",
    "tests/unit/test_input_buffer.c",
    "tests/unit/test_predictor.c",
    "tests/unit/test_spill_buffer.c",
    "tests/unit/test_lexer.c",
    "tests/integration/test_boundary_conditions.c",
    "tests/integration/test_comparison.c",
    "tests/integration/test_regression.c"
)
& $gcc $cflags $testSources $srcFiles -o "$binDir/test_runner.exe" -lm
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to build test_runner.exe"
}
Write-Host "  [OK] Built $binDir/test_runner.exe (all 8 test suites integrated)" -ForegroundColor Green

Write-Host "Compiling benchmark runner..." -ForegroundColor Cyan
& $gcc $cflags "benchmarks/benchmark_runner.c" $srcFiles -o "$binDir/benchmark_runner.exe" -lm
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to build benchmark_runner.exe"
}
Write-Host "  [OK] Built $binDir/benchmark_runner.exe" -ForegroundColor Green

if ($Test -or (-not $Benchmark)) {
    Write-Host "`n>>> RUNNING AUTOMATED TEST SUITE <<<" -ForegroundColor Magenta
    
    & "$binDir/test_runner.exe"
    if ($LASTEXITCODE -ne 0) {
        Write-Host "`nTEST SUITE REPORTED FAILURES!" -ForegroundColor Red
        exit 1
    } else {
        Write-Host "`nALL TEST SUITES PASSED SUCCESSFULLY!" -ForegroundColor Green
    }
}

if ($Benchmark) {
    Write-Host "`n>>> RUNNING BENCHMARKS <<<" -ForegroundColor Magenta
    & "$binDir/benchmark_runner.exe" @args
}
