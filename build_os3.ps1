# PowerShell script to build for AmigaOS 3 using Docker

$ErrorActionPreference = "Stop"

# Define the Docker image
$Image = "amigadev/crosstools:m68k-amigaos"

# Define the build command
# We mirror the steps from .github/workflows/makefile.yml
# 1. cd mcc
# 2. make OS=os3 DEBUG=
# 3. cd ../mcp
# 4. make OS=os3 DEBUG=
$BuildCommand = "mkdir -p mcc/.obj_os3/classes mcc/bin_os3 && cd mcc && make bin_os3/HTMLview.mcc OS=os3 DEBUG= > ../build.log 2>&1 && make bin_os3/SimpleTest OS=os3 DEBUG= >> ../build.log 2>&1 && cd ../mcp && make OS=os3 DEBUG= >> ../build.log 2>&1"

# Get the current directory (project root)
$WorkDir = Get-Location

Write-Host "Starting AmigaOS 3 cross-compilation..."
Write-Host "Mounting '$WorkDir' to '/work' in container '$Image'"

try {
    # Run the Docker container
    # -v "${WorkDir}:/work" mounts the current directory to /work
    # --rm automatically removes the container after exit
    # -w /work sets the working directory to /work
    docker run --rm -v "${WorkDir}:/work" -w /work $Image /bin/bash -c "$BuildCommand"

    if ($LASTEXITCODE -eq 0) {
        Write-Host "Build completed successfully!"
    } else {
        Write-Error "Build failed with exit code $LASTEXITCODE"
    }
} catch {
    Write-Error "An error occurred while running Docker: $_"
}
